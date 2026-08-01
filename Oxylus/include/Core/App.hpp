#pragma once

#include "Core/AppCommandLineArgs.hpp"
#include "Core/EventSystem.hpp"
#include "Core/JobManager.hpp"
#include "Core/ModuleRegistry.hpp"
#include "Core/VFS.hpp"
#include "Render/RenderContext.hpp"
#include "Render/Window.hpp"
#include "Sim/SimHost.hpp"
#include "Utils/Timestep.hpp"

namespace ox {
class ImGuiLayer;
class RenderContext;

struct WindowResizeEvent {
  u32 width = 0;
  u32 height = 0;
};

struct AppCloseEvent {};

class App {
public:
  App(int argc, char** argv);
  ~App();

  static App* get() { return instance_; }
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

  // Modules that declare SIM_MODULE live in the simulation host's registry instead of App's, so
  // simulation code can reach them without including this header (and with it, vuk and SDL).
  template <typename T, typename... Args>
  auto with(this App& self, Args&&... args) -> App& {
    ZoneScoped;

    if constexpr (requires { T::SIM_MODULE; }) {
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
    if constexpr (requires { T::SIM_MODULE; }) {
      return SimHost::mod<T>();
    } else {
      return get()->registry.get<T>();
    }
  }

  template <typename T>
  static auto has_mod() -> bool {
    if constexpr (requires { T::SIM_MODULE; }) {
      return SimHost::has_mod<T>();
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
  std::unique_ptr<SimHost> sim_host = nullptr;

  Timestep timestep = {};
  i32 frame_limit = 0;

  bool is_running = true;

  auto run_deferred_tasks(this App& self) -> void;
};

App* create_application(const AppCommandLineArgs& args);
} // namespace ox
