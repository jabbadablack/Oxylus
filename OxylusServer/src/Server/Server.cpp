#include "Server/Server.hpp"

#include <algorithm>

#include "Core/EventSystem.hpp"
#include "Core/JobManager.hpp"
#include "Core/VFS.hpp"
#include "Networking/NetPacket.hpp"
#include "Networking/NetServer.hpp"
#include "Networking/NetworkManager.hpp"
#include "Scene/Scene.hpp"
#include "Utils/Log.hpp"
#include "Utils/Timer.hpp"

namespace ox {
Server* Server::instance_ = nullptr;

// The shared services a standalone server has to bring itself, because there is no App to own
// them. Defined here rather than in the header so the header stays free of their definitions.
struct Server::OwnedServices {
  VFS vfs = {};
  JobManager job_manager = {};
  EventSystem event_system = {};
  Timestep timestep = {};
};

Server::Server(std::filesystem::path assets_directory)
    : owned_(std::make_unique<OwnedServices>()),
      assets_directory_(std::move(assets_directory)),
      vfs_(owned_->vfs),
      job_manager_(owned_->job_manager),
      event_system_(owned_->event_system),
      timestep_(owned_->timestep) {
  set_instance(this);
}

Server::Server(VFS& vfs, JobManager& job_manager, EventSystem& event_system, const Timestep& timestep)
    : owned_(nullptr),
      vfs_(vfs),
      job_manager_(job_manager),
      event_system_(event_system),
      timestep_(timestep) {
  set_instance(this);
}

Server::~Server() {
  if (instance_ == this) {
    instance_ = nullptr;
  }
}

auto Server::set_instance(Server* instance) -> void { instance_ = instance; }

auto Server::init(this Server& self) -> std::expected<void, std::string> {
  ZoneScoped;

  // Standalone only: App does all of this for a hosted server, and doing it twice would mount the
  // VFS again and start a second worker pool.
  if (self.owned_) {
    self.vfs_.mount_dir(VFS::APP_DIR, std::filesystem::absolute(self.assets_directory_));

    if (const auto result = self.job_manager_.init(); !result.has_value()) {
      return std::unexpected(fmt::format("Failed to initialize the job manager: {}", result.error()));
    }

    if (const auto result = self.event_system_.init(); !result.has_value()) {
      return std::unexpected(fmt::format("Failed to initialize the event system: {}", result.error()));
    }
  }

  if (!self.registry.init()) {
    return std::unexpected("Failed to initialize a simulation module.");
  }

  return {};
}

auto Server::deinit(this Server& self) -> std::expected<void, std::string> {
  ZoneScoped;

  // Before the modules go: NetworkManager asserts its server list is empty on deinit, and it owns
  // the NetServer that listen() created.
  if (self.net_server_ != nullptr) {
    self.registry.get<NetworkManager>().destroy_server(self.net_server_);
    self.net_server_ = nullptr;
  }

  self.scenes_.clear();

  if (!self.registry.deinit()) {
    return std::unexpected("Failed to deinitialize a simulation module.");
  }

  if (self.owned_) {
    self.job_manager_.wait();
    static_cast<void>(self.event_system_.deinit());
    static_cast<void>(self.job_manager_.deinit());
  }

  return {};
}

auto Server::register_scene(this Server& self, const std::shared_ptr<Scene>& scene) -> void {
  if (!scene) {
    return;
  }

  const auto already = std::ranges::any_of(self.scenes_, [&scene](const RegisteredScene& r) {
    return r.scene == scene;
  });

  if (!already) {
    self.scenes_.emplace_back(
      RegisteredScene{
        .scene = scene,
        .id = static_cast<SceneID>(self.scenes_.size()),
      }
    );
  }
}

auto Server::unregister_scene(this Server& self, const Scene* scene) -> void {
  std::erase_if(self.scenes_, [scene](const RegisteredScene& r) { return r.scene.get() == scene; });
}

auto Server::tick(this Server& self, const Timestep& timestep) -> void {
  ZoneScoped;

  // Before anything simulates: the world advances from a state the client has already been told
  // about, so its edit and the frame it sees cannot disagree.
  self.apply_pending_commands();

  for (const auto& registered : self.scenes_) {
    registered.scene->runtime_update(timestep);
  }

  // Network last, so a client sees the world as it stands at the end of the tick rather than
  // halfway through it.
  if (self.net_server_ != nullptr) {
    if (self.net_server_->tick(timestep)) {
      self.replicate();
    }

    // Checked every tick, not only on the disconnect callback. A client that aborts never sends a
    // clean disconnect, so the only signal is ENet noticing the peer has gone quiet - and this is
    // what guarantees the server follows it down instead of lingering.
    if (self.had_client_ && self.net_server_->remote_clients.size() == 0) {
      OX_LOG_INFO("No clients remain; shutting down.");
      self.should_exit_ = true;
    }
  }
}

auto Server::tick_owned(this Server& self) -> const Timestep& {
  ZoneScoped;

  OX_CHECK_NULL(self.owned_);

  self.owned_->timestep.on_update();
  self.tick(self.owned_->timestep);

  return self.owned_->timestep;
}

auto Server::set_tick_rate(this Server& self, const f64 ticks_per_second) -> void {
  OX_CHECK_NULL(self.owned_);

  if (ticks_per_second > 0.0) {
    self.owned_->timestep.set_max_frame_time(1000.0 / ticks_per_second);
  } else {
    self.owned_->timestep.reset_max_frame_time();
  }
}

// The engine's NetServer with the replication callbacks filled in. It lives here rather than in a
// header because nothing outside this file needs to name it - NetworkManager owns the instance and
// Server keeps a bare pointer to it.
struct SceneNetServer : NetServer {
  Server* owner = nullptr;

  SceneNetServer(ENetHost* host, Server* owner_) : NetServer(host), owner(owner_) {}

  auto on_client_connect(NetClientID client_id) -> void override {
    OX_LOG_INFO("Client connected; sending full scene state.");
    owner->on_client_connected(client_id);
  }

  auto on_client_disconnect(NetClientID client_id) -> void override { owner->on_client_disconnected(client_id); }

  auto on_client_ack(NetClientID client_id, NetClientAckPacket& packet) -> void override {
    owner->on_client_acked(packet.acked);
  }
};

auto Server::listen(this Server& self, const u16 port, const u32 max_clients) -> bool {
  ZoneScoped;

  auto& network = self.registry.get<NetworkManager>();
  self.net_server_ = network.create_server<SceneNetServer>(port, max_clients, &self);

  if (self.net_server_ == nullptr) {
    OX_LOG_ERROR("Could not bind port {}.", port);
    return false;
  }

  // One entry point for every world mutation that does not come from a system. The client sends a
  // serialised ServerCommand; it is queued here and applied at the top of the next tick.
  self.net_server_->register_proc("ox.command", [&self](NetClientID, std::span<RPCParameter> params) {
    if (params.empty()) {
      return;
    }

    const auto bytes = params[0].as_span<u8>();
    if (auto command = deserialize_command(bytes)) {
      self.pending_commands_.emplace_back(std::move(command.value()));
    }
  });

  // The 20 Hz default is tuned for the internet. This is a loopback editor session, where the cost
  // of a tick is a memcpy and the benefit is the editor not feeling laggy.
  self.net_server_->set_tick_rate(60.0);

  OX_LOG_INFO("Listening on port {}.", port);
  return true;
}

auto Server::on_client_connected(this Server& self, const NetClientID client_id) -> void {
  self.had_client_ = true;

  // A new client knows nothing, so it gets the world whole. Everything after this is a delta.
  for (auto& registered : self.scenes_) {
    registered.builder = {};
    SceneSnapshotBuilder::take_snapshot(registered.scene->world, registered.builder.current());
    self.send_state(client_id, registered, registered.builder.current());
  }
}

auto Server::on_client_disconnected(this Server& self, NetClientID) -> void {
  if (self.net_server_ != nullptr && self.net_server_->remote_clients.size() <= 1 && self.had_client_) {
    OX_LOG_INFO("Last client disconnected; shutting down.");
    self.should_exit_ = true;
  }
}

auto Server::on_client_acked(this Server& self, const u8 sequence) -> void {
  for (auto& registered : self.scenes_) {
    registered.builder.ack(sequence);
  }
}

auto Server::send_state(
  this Server& self, const NetClientID client_id, RegisteredScene& registered, const SceneState& state
) -> void {
  auto* client = self.net_server_->remote_clients.slot(client_id);
  if (client == nullptr) {
    return;
  }

  auto packet = NetPacket::scene_snapshot(state, registered.builder.current_sequence, registered.id);
  if (!packet.has_value()) {
    OX_LOG_ERROR("Could not serialise a scene snapshot.");
    return;
  }

  // No destroy(): on a successful send ENet takes ownership of the packet and frees it once it
  // has gone out. send_unreliable only destroys it when the send itself fails.
  client->send_unreliable(packet.value());
}

auto Server::apply_pending_commands(this Server& self) -> void {
  ZoneScoped;

  if (self.pending_commands_.empty()) {
    return;
  }

  // Commands do not name a scene yet, so they go to the first registered one. That is exactly
  // right while the editor mirrors a single world and wrong the moment it mirrors two - revisit
  // when scene creation itself becomes a command.
  if (self.scenes_.empty()) {
    self.pending_commands_.clear();
    return;
  }

  auto& scene = *self.scenes_.front().scene;
  for (const auto& command : self.pending_commands_) {
    static_cast<void>(apply_command(scene, command));
  }

  self.pending_commands_.clear();
}

auto Server::replicate(this Server& self) -> void {
  ZoneScoped;

  if (self.net_server_ == nullptr || self.net_server_->remote_clients.size() == 0) {
    return;
  }

  for (auto& registered : self.scenes_) {
    registered.builder.advance();
    registered.builder.current().clear();
    SceneSnapshotBuilder::take_snapshot(registered.scene->world, registered.builder.current());

    const auto delta = registered.builder.delta();
    if (delta.entities.empty() && delta.removed_entities.empty()) {
      continue;
    }

    for (auto& client : self.net_server_->remote_clients.slots_unsafe()) {
      auto packet = NetPacket::scene_snapshot(delta, registered.builder.current_sequence, registered.id);
      if (!packet.has_value()) {
        continue;
      }

      client.send_unreliable(packet.value());
    }
  }
}

auto Server::create_default_scene(this Server& self, const std::string& name) -> std::shared_ptr<Scene> {
  ZoneScoped;

  auto scene = std::make_shared<Scene>();
  scene->init(name);

  // The same starting world the editor used to build for itself. It is authored here now, because
  // the server owns the world and the client only ever mirrors one.
  const auto sun = scene->create_entity("sun");
  sun.set<TransformComponent>({.rotation = glm::quat(glm::vec3(glm::radians(45.f), glm::radians(90.f), 0.f))});
  sun.set<LightComponent>({.type = LightComponent::LightType::Directional, .intensity = 10.f})
    .add<AtmosphereComponent>();
  sun.set<AutoExposureComponent>({});

  const auto camera = scene->create_entity("camera");
  camera.set<TransformComponent>({});
  camera.set<CameraComponent>({});

  self.register_scene(scene);
  return scene;
}

auto Server::load_scene(this Server& self, const std::filesystem::path& path) -> std::shared_ptr<Scene> {
  ZoneScoped;

  auto scene = std::make_shared<Scene>();
  scene->init(path.stem().string());

  if (!scene->load_from_file(path)) {
    OX_LOG_ERROR("Could not load scene \"{}\".", path);
    return nullptr;
  }

  self.register_scene(scene);
  return scene;
}

auto Server::get_vfs() -> VFS& {
  OX_CHECK_NULL(instance_);
  return instance_->vfs_;
}

auto Server::get_job_manager() -> JobManager& {
  OX_CHECK_NULL(instance_);
  return instance_->job_manager_;
}

auto Server::get_event_system() -> EventSystem& {
  OX_CHECK_NULL(instance_);
  return instance_->event_system_;
}

auto Server::get_timestep() -> const Timestep& {
  OX_CHECK_NULL(instance_);
  return instance_->timestep_;
}
} // namespace ox
