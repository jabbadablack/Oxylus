#pragma once

#include <expected>

#include "Core/ModuleRegistry.hpp"
#include "Utils/Timestep.hpp"

namespace ox {
class VFS;
class JobManager;
class EventSystem;

// The simulation's own host: it owns the headless module registry and hands simulation code the
// shared services, so nothing on that side has to reach for App.
//
// This exists because App owns the window and the render context. Any simulation source that called
// App::mod<T>() therefore dragged vuk and SDL in behind it, which is what kept Scene, Physics,
// Scripting and Networking out of the headless target. A dedicated server is a SimHost and a main();
// in the editor, App owns one.
//
// Modules opt in by declaring `constexpr static bool SIM_MODULE = true;`. App::with<T>() and
// App::mod<T>() route those here automatically, so presentation code keeps its existing spelling.
class SimHost {
public:
  SimHost(VFS& vfs, JobManager& job_manager, EventSystem& event_system, const Timestep& timestep);
  ~SimHost();

  SimHost(const SimHost&) = delete;
  SimHost& operator=(const SimHost&) = delete;

  static auto get() -> SimHost* { return instance_; }
  static auto set_instance(SimHost* instance) -> void;

  auto init(this SimHost& self) -> std::expected<void, std::string>;
  auto deinit(this SimHost& self) -> std::expected<void, std::string>;

  template <typename T, typename... Args>
  auto add(this SimHost& self, Args&&... args) -> void {
    self.registry.add<T>(std::forward<Args>(args)...);
  }

  template <typename T>
  static auto mod() -> T& {
    return get()->registry.get<T>();
  }

  template <typename T>
  static auto has_mod() -> bool {
    return get()->registry.has<T>();
  }

  static auto get_vfs() -> VFS&;
  static auto get_job_manager() -> JobManager&;
  static auto get_event_system() -> EventSystem&;
  static auto get_timestep() -> const Timestep&;

private:
  static SimHost* instance_;

  VFS& vfs_;
  JobManager& job_manager_;
  EventSystem& event_system_;
  const Timestep& timestep_;

  ModuleRegistry registry = {};
};
} // namespace ox
