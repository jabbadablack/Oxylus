#include "Core/App.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_timer.h>
#include <vuk/vsl/Core.hpp>

#include "Asset/AssetManager.hpp"
#include "Core/EventSystem.hpp"
#include "Core/Input.hpp"
#include "Core/JobManager.hpp"
#include "Core/VFS.hpp"
#include "Networking/NetClient.hpp"
#include "Networking/NetworkManager.hpp"
#include "Render/RenderContext.hpp"
#include "Render/Renderer.hpp"
#include "Render/Window.hpp"
#include "Scripting/LuaManager.hpp"
#include "Server/ServerCommand.hpp"
#include "UI/ImGuiRenderer.hpp"
#include "Utils/Profiler.hpp"

namespace ox {
App* App::instance_ = nullptr;

App::App(int argc, char** argv) {
  ZoneScoped;

  if (instance_) {
    OX_LOG_ERROR("Application already exists!");
    return;
  }

  instance_ = this;

  if (const auto* base_path = SDL_GetBasePath())
    std::filesystem::current_path(base_path);

  Log::init(argc, argv);

  instance_->command_line_args = AppCommandLineArgs{argc, argv};

  // Built here rather than in init(): with<T>() runs against the builder before init(), and
  // simulation modules have to have somewhere to land.
  sim_host = std::make_unique<Server>(vfs, job_manager, event_system, timestep);
}

App::~App() {
  is_running = false;
  instance_ = nullptr;
}

void App::set_instance(App* instance) { instance_ = instance; }

auto App::init(this App& self) -> void {
  if (self.command_line_args.contains("--verbose") || self.command_line_args.contains("-v")) {
    Log::set_verbose();
    OX_LOG_TRACE("Enabled verbose logging.");
  }

  if (self.working_directory.empty())
    self.working_directory = std::filesystem::current_path();
  else
    std::filesystem::current_path(self.working_directory);

  self.vfs.mount_dir(VFS::APP_DIR, std::filesystem::absolute(self.assets_path));

  // Spawned here, before the window and the Vulkan context, deliberately. The server needs a couple
  // of hundred milliseconds to boot and bind its port; the client spends far longer than that
  // creating a window and a render context. Starting it first means the two overlap and the
  // connect below finds it already listening, instead of adding its startup to ours.
  if (self.wants_server && !self.spawn_server()) {
    OX_LOG_FATAL("Could not start {}. The editor is a client and cannot run without it.", OX_SERVER_EXECUTABLE);
    self.is_running = false;
    return;
  }

  if (self.window_info.has_value()) {
    self.window = Window::create(*self.window_info);
  }

  if (self.registry.has<Renderer>()) {
    self.render_context = std::make_unique<RenderContext>();

    const bool enable_validation = self.command_line_args.contains("--vulkan-validation");
    self.render_context->create_context(*self.window, enable_validation);
  }

  auto job_manager_init_result = self.job_manager.init();
  if (job_manager_init_result.has_value())
    OX_LOG_INFO("Initalized JobManager.");
  else
    OX_LOG_ERROR("Failed to initalize JobManager: {}", job_manager_init_result.error());

  auto event_system_init_result = self.event_system.init();
  if (event_system_init_result.has_value())
    OX_LOG_INFO("Initalized EventSystem.");
  else
    OX_LOG_ERROR("Failed to initalize EventSystem: {}", event_system_init_result.error());

  // Before sim_host->init(), not after: the registry is simulation-side but materialising a model
  // or a texture is GPU work, so the client hands its loaders over first. Installing them late
  // would silently no-op any asset loaded during initialisation.
  install_client_asset_loaders();

  if (const auto sim_init_result = self.sim_host->init(); !sim_init_result.has_value()) {
    OX_LOG_ERROR("Failed to initalize simulation: {}", sim_init_result.error());
  }

  // NetworkManager lives in the server-side registry, so the client can only be created once that
  // is up - and it has to happen before the editor modules init, because they come up expecting a
  // world to read.
  if (self.wants_server) {
    if (const auto result = self.connect_to_server(); !result.has_value()) {
      OX_LOG_FATAL("Could not reach the server: {}", result.error());
      self.is_running = false;
      return;
    }
  }

  self.registry.init();

  // Presentation-side Lua bindings are handed to the simulation's LuaManager once the modules they
  // name actually exist.
  bind_client_lua_bindings();

  self.job_manager.wait();
}

auto App::with_server(this App& self, const u16 port) -> App& {
  self.wants_server = true;
  self.server_port = port;
  return self;
}

auto App::spawn_server(this App& self) -> bool {
  ZoneScoped;

  // --server-port lets a second editor use its own server; --attach-server skips spawning
  // altogether so the server can be run under a debugger and connected to.
  // Non-const: option::value() forwards `self` as a non-const rvalue, so it does not compile on a
  // const option. See the note in OxylusServer/main.cpp.
  if (auto index = self.command_line_args.get_index("--server-port")) {
    auto value = self.command_line_args.get(index.value() + 1);
    if (value.has_value()) {
      self.server_port = static_cast<u16>(std::stoi(value->arg_str));
    }
  }

  if (self.command_line_args.contains("--attach-server")) {
    OX_LOG_INFO("--attach-server: expecting a server already listening on port {}.", self.server_port);
    return true;
  }

  const auto executable = (std::filesystem::current_path() / OX_SERVER_EXECUTABLE).string();
  const auto port = std::to_string(self.server_port);

  // Child stdio is inherited rather than piped, so the server's log lands in the same console as
  // ours. A crash in another process is hard enough to see without hiding its output.
  const char* argv[] = {executable.c_str(), "--port", port.c_str(), nullptr};
  auto* process = SDL_CreateProcess(argv, false);
  if (process == nullptr) {
    OX_LOG_ERROR("SDL_CreateProcess failed: {}", SDL_GetError());
    return false;
  }

  self.server_process = process;
  OX_LOG_INFO("Spawned {} on port {}.", OX_SERVER_EXECUTABLE, self.server_port);

  return true;
}

auto App::connect_to_server(this App& self) -> std::expected<void, std::string> {
  ZoneScoped;

  auto& network = Server::mod<NetworkManager>();
  self.server_client = network.create_client();
  if (self.server_client == nullptr) {
    return std::unexpected("could not create a network client");
  }

  self.server_client->set_tick_rate(60.0);

  constexpr auto CONNECT_TIMEOUT_MS = 10000.0;

  if (!self.server_client->connect("127.0.0.1", self.server_port, CONNECT_TIMEOUT_MS)) {
    return std::unexpected("could not start connecting");
  }

  // connect() only starts the handshake; it completes inside tick(). Pump it here rather than
  // letting the first frames run unconnected, because everything downstream expects a world.
  const auto deadline = SDL_GetTicks() + static_cast<u64>(CONNECT_TIMEOUT_MS);
  while (SDL_GetTicks() < deadline) {
    self.timestep.on_update();
    self.server_client->tick(self.timestep);

    if (self.server_client->status == NetClientStatus::Connected) {
      OX_LOG_INFO("Connected to the server on port {}.", self.server_port);
      return {};
    }

    if (
      self.server_client->status == NetClientStatus::TimedOut ||
      self.server_client->status == NetClientStatus::Disconnected
    ) {
      break;
    }

    SDL_Delay(1);
  }

  return std::unexpected(fmt::format("no response on port {} within {} ms", self.server_port, CONNECT_TIMEOUT_MS));
}

auto App::send_command(const ServerCommand& command) -> void {
  ZoneScoped;

  auto* self = get();
  if (self == nullptr || self->server_client == nullptr) {
    OX_LOG_ERROR("Dropped an edit: not connected to a server.");
    return;
  }

  const auto bytes = serialize_command(command);
  const auto params = std::array{RPCParameter{.value = std::vector<u8>(bytes.begin(), bytes.end())}};

  auto packet = NetPacket::rpc("ox.command", params);
  if (!packet.has_value()) {
    OX_LOG_ERROR("Could not serialise an edit.");
    return;
  }

  // Reliable: a lost edit is not something replication can repair, unlike a lost snapshot.
  self->server_client->send_reliable(packet.value());
}

auto App::is_connected_to_server(this const App& self) -> bool {
  return self.server_client != nullptr && self.server_client->status == NetClientStatus::Connected;
}

auto App::disconnect_from_server(this App& self) -> void {
  ZoneScoped;

  if (self.server_client != nullptr) {
    self.server_client->disconnect(false);

    // NetworkManager owns the client and asserts its list is empty on deinit, so dropping the
    // pointer is not enough - it has to be handed back.
    Server::mod<NetworkManager>().destroy_client(self.server_client);
    self.server_client = nullptr;
  }

  if (self.server_process != nullptr) {
    auto* process = static_cast<SDL_Process*>(self.server_process);

    // The server also exits on its own once the last client drops, so give it a moment to do that
    // cleanly before killing it - a killed process never runs its deinit.
    auto exit_code = 0;
    for (auto i = 0; i < 50 && !SDL_WaitProcess(process, false, &exit_code); ++i) {
      SDL_Delay(10);
    }

    SDL_KillProcess(process, false);
    SDL_DestroyProcess(process);
    self.server_process = nullptr;

    OX_LOG_INFO("Server process stopped.");
  }
}

auto App::step(this App& self) -> void {
  // render_context only exists when a Renderer module was registered; a headless App has none.
  const auto configured_frame_limit = self.render_context ? self.render_context->context_cvar.cvar_frame_limit.get()
                                                          : 0;
  const i32 frame_limit = self.frame_limit > 0 ? self.frame_limit : configured_frame_limit;
  if (frame_limit > 0) {
    self.timestep.set_max_frame_time(1000.0 / static_cast<f64>(frame_limit));
  } else {
    self.timestep.reset_max_frame_time();
  }

  self.timestep.on_update();

  self.run_deferred_tasks();

  if (self.window.has_value())
    self.window->update(self.timestep);

  if (!self.is_running)
    return;

  // Pumped before the modules run so the world they read is this frame's, not last frame's.
  if (self.server_client != nullptr) {
    self.server_client->tick(self.timestep);
  }

  self.registry.update(self.timestep);

  if (self.registry.has<Input>())
    self.mod<Input>().reset_pressed();
}

void App::run(this App& self) {
  ZoneScoped;

  self.init();

  while (self.is_running) {
    self.step();
    FrameMark;
  }

  self.stop();
}

void App::stop(this App& self) {
  ZoneScoped;

  self.is_running = false;

  // Before the modules go away: the client belongs to NetworkManager, and the server should be
  // told we are leaving rather than discovering it by timeout.
  self.disconnect_from_server();

  // Single point where the close is announced, so it fires no matter how the loop was left.
  std::ignore = self.event_system.emit<AppCloseEvent>(AppCloseEvent{});

  // Anything queued for "next frame" never got one. Run it while every module is still alive,
  // since those callbacks are how deferred destruction is expressed.
  self.run_deferred_tasks();

  self.job_manager.wait();

  // Modules release GPU resources in their deinit/destructor, so nothing may still be in flight.
  if (self.render_context != nullptr) {
    self.render_context->wait();
  }

  // Presentation first, then simulation - the mirror of init, so nothing tears down a module its
  // dependents are still using.
  self.registry.deinit();
  if (const auto sim_deinit_result = self.sim_host->deinit(); !sim_deinit_result.has_value()) {
    OX_LOG_ERROR("Failed to deinitalize simulation: {}", sim_deinit_result.error());
  }

  self.job_manager.wait();
  self.run_deferred_tasks();

  auto job_manager_deinit_result = self.job_manager.deinit();
  if (job_manager_deinit_result.has_value())
    OX_LOG_INFO("Deinitalized JobManager.");
  else
    OX_LOG_ERROR("Failed to deinitalize JobManager: {}", job_manager_deinit_result.error());

  auto event_system_deinit_result = self.event_system.deinit();
  if (event_system_deinit_result.has_value())
    OX_LOG_INFO("Deinitalized EventSystem.");
  else
    OX_LOG_ERROR("Failed to deinitalize EventSystem: {}", event_system_deinit_result.error());

  // The surface outlives the swapchain, and both outlive the window they were created from.
  if (self.render_context != nullptr) {
    self.render_context->destroy_context();
  }
  if (self.window.has_value()) {
    self.window->destroy();
  }
}

auto App::should_stop(this App& self) -> void {
  self.is_running = false; //
}

auto App::with_name(this App& self, std::string name) -> App& {
  self.name = name;
  return self;
}

auto App::with_window(this App& self, WindowInfo window_info) -> App& {
  self.window_info = window_info;
  return self;
}

auto App::with_working_directory(this App& self, const std::filesystem::path& dir) -> App& {
  self.working_directory = dir;
  return self;
}

auto App::with_assets_directory(this App& self, const std::filesystem::path& dir) -> App& {
  self.assets_path = dir;
  return self;
}

auto App::with_workers(this App& self, const u32 count) -> App& {
  self.job_manager.set_thread_count(count);
  return self;
}

auto App::run_deferred_tasks(this App& self) -> void {
  {
    auto lock = std::unique_lock(self.mutex);
    std::swap(self.pending_tasks, self.processing_tasks);
  }

  for (auto& task : self.processing_tasks) {
    if (task) {
      task();
    }
  }

  self.processing_tasks.clear();
}

auto App::get_command_line_args(this const App& self) -> const AppCommandLineArgs& {
  return self.command_line_args; //
}

auto App::get_window() -> const Window& {
  OX_ASSERT(instance_->window.has_value());
  return instance_->window.value();
}

auto App::get_rendercontext() -> RenderContext& {
  return *instance_->render_context; //
}

auto App::get_timestep() -> const Timestep& {
  return instance_->timestep; //
}

auto App::get_vfs() -> VFS& {
  return instance_->vfs; //
}

auto App::get_job_manager() -> JobManager& {
  return instance_->job_manager; //
}

auto App::get_event_system() -> EventSystem& {
  return instance_->event_system; //
}
} // namespace ox
