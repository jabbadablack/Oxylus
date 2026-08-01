#include "Core/App.hpp"

#include <SDL3/SDL_filesystem.h>
#include <vuk/vsl/Core.hpp>

#include "Core/EventSystem.hpp"
#include "Core/Input.hpp"
#include "Core/JobManager.hpp"
#include "Core/VFS.hpp"
#include "Render/RenderContext.hpp"
#include "Render/Renderer.hpp"
#include "Render/Window.hpp"
#include "Scripting/LuaManager.hpp"
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
  sim_host = std::make_unique<SimHost>(vfs, job_manager, event_system, timestep);
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

  if (const auto sim_init_result = self.sim_host->init(); !sim_init_result.has_value()) {
    OX_LOG_ERROR("Failed to initalize simulation: {}", sim_init_result.error());
  }

  self.registry.init();

  // Presentation-side Lua bindings are handed to the simulation's LuaManager once the modules they
  // name actually exist.
  bind_client_lua_bindings();

  self.job_manager.wait();
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
