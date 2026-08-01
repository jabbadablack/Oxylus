#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "Asset/Fwd.hpp"
#include "Core/ModuleRegistry.hpp"
#include "Networking/Fwd.hpp"
#include "Scene/SceneSnapshot.hpp"
#include "Utils/Timestep.hpp"

namespace ox {
class VFS;
class JobManager;
class EventSystem;
class Scene;
struct NetServer;

// The procs this file implements: the ones deciding which worlds exist. Anything acting on the
// contents of a world is declared by Scene instead. Named constants, not literals - a proc name
// is matched at runtime, so a typo would otherwise be a silent no-op on both sides.
namespace proc {
constexpr auto SCENE_CREATE = std::string_view("scene.create");
constexpr auto SCENE_PLAY = std::string_view("scene.play");
constexpr auto SCENE_STOP = std::string_view("scene.stop");
constexpr auto SCENE_DESTROY = std::string_view("scene.destroy");
constexpr auto SCENE_LOAD = std::string_view("scene.load");
} // namespace proc

// The simulation's own host: it owns the headless module registry and hands simulation code the
// shared services, so nothing on that side has to reach for App.
//
// This exists because App owns the window and the render context. Any simulation source that called
// App::mod<T>() therefore dragged vuk and SDL in behind it, which is what kept Scene, Physics,
// Scripting and Networking out of the headless target. A dedicated server is a Server and a main();
// in the editor, App owns one.
//
// Modules opt in by declaring `constexpr static bool SERVER_MODULE = true;`. App::with<T>() and
// App::mod<T>() route those here automatically, so presentation code keeps its existing spelling.
class Server {
public:
  // Standalone: the server process has no App, so it owns the shared services itself and brings
  // them up in init(). This is the constructor OxylusServer's main() uses.
  explicit Server(std::filesystem::path assets_directory = "Resources");

  // Hosted: the editor's App already owns these, so borrow them rather than running a second set.
  Server(VFS& vfs, JobManager& job_manager, EventSystem& event_system, const Timestep& timestep);

  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  // Out of line on purpose. Inline, every client translation unit read instance_ directly, which
  // makes it a cross-library *data* reference - and data, unlike functions, is not something
  // WINDOWS_EXPORT_ALL_SYMBOLS can paper over: the consumer needs __declspec(dllimport) to reach
  // it. As a call it is an ordinary exported function and the client half links clean.
  static auto get() -> Server*;
  static auto set_instance(Server* instance) -> void;

  auto init(this Server& self) -> std::expected<void, std::string>;
  auto deinit(this Server& self) -> std::expected<void, std::string>;

  // Scenes the simulation ticks. The client registers them; the simulation does not know what an
  // editor tab or a viewport is, so it will not stop ticking a scene just because nothing is
  // looking at it. That was the old behaviour: the tick lived inside the viewport panel loop, so a
  // scene simulated only while a panel drew it, and twice per frame if two panels did.
  auto register_scene(this Server& self, const std::shared_ptr<Scene>& scene) -> void;
  auto unregister_scene(this Server& self, const Scene* scene) -> void;

  // Runs one tick of every registered scene. The single tick point for the whole simulation: the
  // editor calls it once a frame, and the server process will call it from its own loop.
  auto tick(this Server& self, const Timestep& timestep) -> void;

  // Standalone only: advances the owned Timestep and ticks once, pacing to the configured rate.
  // Returns the Timestep it advanced so a caller can report frame times.
  auto tick_owned(this Server& self) -> const Timestep&;

  // Standalone only. Ticks per second; the owned Timestep sleeps out the remainder of each tick.
  auto set_tick_rate(this Server& self, f64 ticks_per_second) -> void;

  // Loads a scene from disk, registers it, and returns it. Null on failure.
  auto load_scene(this Server& self, const std::filesystem::path& path) -> std::shared_ptr<Scene>;

  // Creates and registers the default starting world - a sun and a camera. The client no longer
  // authors this; it mirrors whatever the server has.
  auto create_default_scene(this Server& self, const std::string& name = "Untitled") -> std::shared_ptr<Scene>;

  auto scene_count(this const Server& self) -> usize { return self.scenes_.size(); }

  // The scene a proc acts on when nothing names one. Procs carry no scene id yet, unlike
  // snapshots, so everything lands on the first registered world.
  auto primary_scene(this const Server& self) -> Scene*;

  // Registers the procs declared above. A member because they reach the scene registry.
  static auto register_procs(NetServer& net, Server& server) -> void;

  // Starts listening for clients. Replication begins as soon as one connects: a full state first,
  // then a delta per network tick. Returns false if the port could not be bound.
  auto listen(this Server& self, u16 port, u32 max_clients = 4) -> bool;

  // True once the last client has gone. A server with nobody attached has nothing to simulate for,
  // and this is what stops a crashed editor from orphaning the process.
  auto should_exit(this const Server& self) -> bool { return self.should_exit_; }

  template <typename T, typename... Args>
  auto add(this Server& self, Args&&... args) -> void {
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
  // Declared before the references below so it is constructed first - they bind into it. Only the
  // standalone constructor fills it in; the hosted one leaves it null and borrows App's services.
  struct OwnedServices;
  // No `= nullptr`: a default member initializer would instantiate unique_ptr's deleter here,
  // where OwnedServices is still incomplete. The constructors in the .cpp do it instead.
  std::unique_ptr<OwnedServices> owned_;

  std::filesystem::path assets_directory_ = {};

  static Server* instance_;

  VFS& vfs_;
  JobManager& job_manager_;
  EventSystem& event_system_;
  const Timestep& timestep_;

  ModuleRegistry registry = {};

  // A scene plus the replication state for it. The id is the registration order, which is all the
  // client needs to tell one replica from another.
  struct RegisteredScene {
    std::shared_ptr<Scene> scene = nullptr;
    SceneID id = SceneID::Invalid;
    SceneSnapshotBuilder builder = {};
    bool playing = false;
  };

  auto replicate(this Server& self) -> void;
  auto send_state(this Server& self, NetClientID client_id, RegisteredScene& registered, const SceneState& state)
    -> void;

public:
  // Called by the NetServer subclass in Server.cpp. Public only because that type is defined there.
  auto on_client_connected(this Server& self, NetClientID client_id) -> void;
  auto on_client_disconnected(this Server& self, NetClientID client_id) -> void;
  auto on_client_acked(this Server& self, u8 sequence) -> void;

private:
  std::vector<RegisteredScene> scenes_ = {};

  // Never reused. Deriving an id from scenes_.size() meant a destroyed scene freed its id for
  // the next one, and a client still holding the old mapping applied state to the wrong replica.
  u64 next_scene_id_ = 0;
  NetServer* net_server_ = nullptr;
  bool had_client_ = false;
  bool should_exit_ = false;
};
} // namespace ox
