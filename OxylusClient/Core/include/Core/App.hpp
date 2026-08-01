#pragma once

#include "Core/AppCommandLineArgs.hpp"
#include "Core/EventSystem.hpp"
#include "Core/JobManager.hpp"
#include "Core/ModuleRegistry.hpp"
#include "Core/VFS.hpp"
#include "Render/RenderContext.hpp"
#include "Render/Window.hpp"
#include "Server/Server.hpp"
#include "Utils/Timestep.hpp"

namespace ox {
class ImGuiLayer;
class RenderContext;
struct NetClient;
struct RPCParameter;

struct WindowResizeEvent {
  u32 width = 0;
  u32 height = 0;
};

struct AppCloseEvent {};

class App {
public:
  App(int argc, char** argv);
  ~App();

  // Out of line on purpose, like Server::get(). Inline, every consumer - the editor most of all -
  // read instance_ directly, which makes it a cross-library *data* reference; data, unlike
  // functions, is not something WINDOWS_EXPORT_ALL_SYMBOLS can cover, so the consumer would need
  // __declspec(dllimport) to reach it. As a call it is an ordinary exported function.
  static App* get();
  static void set_instance(App* instance);

  auto init(this App& self) -> void;
  auto step(this App& self) -> void;
  auto run(this App& self) -> void;
  auto stop(this App& self) -> void;
  auto should_stop(this App& self) -> void;

  auto with_name(this App& self, std::string name) -> App&;
  auto with_window(this App& self, WindowInfo window_info) -> App&;
  auto with_working_directory(this App& self, const std::filesystem::path& dir) -> App&;
  auto with_assets_directory(this App& self, const std::filesystem::path& dir) -> App&;

  // Sets the number of worker threads for the JobManager, overriding the default hardware-based auto-detection.
  auto with_workers(this App& self, const u32 count) -> App&;

  template <typename F>
  static void defer_to_next_frame(F&& func) {
    std::function<void()> task = std::forward<F>(func);

    auto lock = std::unique_lock(get()->mutex);
    get()->pending_tasks.push_back(std::move(task));
  }

  // Modules that declare SERVER_MODULE live in the simulation host's registry instead of App's, so
  // simulation code can reach them without including this header (and with it, vuk and SDL).
  template <typename T, typename... Args>
  auto with(this App& self, Args&&... args) -> App& {
    ZoneScoped;

    if constexpr (requires { T::SERVER_MODULE; }) {
      self.sim_host->add<T>(std::forward<Args>(args)...);
    } else {
      self.registry.add<T>(std::forward<Args>(args)...);
    }

    return self;
  }

  template <typename... Modules>
  auto with(this App& self, std::tuple<Modules...>) -> App& {
    (..., [&] {
      self.with<Modules>();
    }());

    return self;
  }

  // Kept as the single spelling for presentation code: simulation modules are forwarded, so a
  // caller does not have to know which side of the boundary a module lives on.
  template <typename T>
  static auto mod() -> T& {
    if constexpr (requires { T::SERVER_MODULE; }) {
      return Server::mod<T>();
    } else {
      return get()->registry.get<T>();
    }
  }

  template <typename T>
  static auto has_mod() -> bool {
    if constexpr (requires { T::SERVER_MODULE; }) {
      return Server::has_mod<T>();
    } else {
      return get()->registry.has<T>();
    }
  }

  auto with_frame_limit(this App& self, i32 frame_limit) -> App& {
    self.frame_limit = frame_limit;
    return self;
  }

  auto get_command_line_args(this const App& self) -> const AppCommandLineArgs&;

  static auto get_window() -> const Window&;
  static auto get_rendercontext() -> RenderContext&;
  static auto get_timestep() -> const Timestep&;
  static auto get_vfs() -> VFS&;
  static auto get_job_manager() -> JobManager&;
  static auto get_event_system() -> EventSystem&;

  // The editor is a client. The authoritative world lives in an OxylusServer process that App
  // spawns and connects to over loopback; everything the panels read is a replica fed by
  // ClientSceneSnapshotEvent. There is deliberately no in-process fallback - if the server cannot
  // be started or reached, that is a hard failure, so there is exactly one code path to maintain.
  // Opt in to the server. An App only spawns and connects to an OxylusServer process when it
  // actually needs a simulated world - the editor does, a headless test that just wants the module
  // registry does not, and having every App start a game server makes them collide on the port.
  auto with_server(this App& self, u16 port = 7777) -> App&;

private:
  auto spawn_server(this App& self) -> bool;
  auto connect_to_server(this App& self) -> std::expected<void, std::string>;
  auto disconnect_from_server(this App& self) -> void;

  auto is_connected_to_server(this const App& self) -> bool;

public:
  // Calls a proc the server registered. The single way client code changes the world, and the
  // same call a game makes - the editor is not privileged here, it just knows more proc names.
  static auto send_rpc(std::string_view proc, std::span<const RPCParameter> params) -> void;

private:
private:
  static App* instance_;

  std::shared_mutex mutex;
  std::vector<std::function<void()>> pending_tasks;
  std::vector<std::function<void()>> processing_tasks;

  std::string name = "Oxylus App";
  std::filesystem::path assets_path = "Resources";
  std::filesystem::path working_directory = {};
  AppCommandLineArgs command_line_args = {};
  option<WindowInfo> window_info = nullopt;

  std::unique_ptr<RenderContext> render_context = nullptr;
  option<Window> window = nullopt;

  VFS vfs = {};
  JobManager job_manager = {};
  EventSystem event_system = {};
  ModuleRegistry registry = {};
  std::unique_ptr<Server> sim_host = nullptr;

  // Owned by NetworkManager, not by App - App only holds the handle it connects with.
  NetClient* server_client = nullptr;
  // SDL_Process*, kept opaque so this header does not pull SDL in.
  void* server_process = nullptr;
  u16 server_port = 7777;
  bool wants_server = false;

  Timestep timestep = {};
  i32 frame_limit = 0;

  bool is_running = true;

  auto run_deferred_tasks(this App& self) -> void;
};

App* create_application(const AppCommandLineArgs& args);
} // namespace ox
