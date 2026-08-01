#include "Server/Server.hpp"

#include <algorithm>
#include <fmt/format.h>

#include "Core/EventSystem.hpp"
#include "Core/JobManager.hpp"
#include "Core/VFS.hpp"
#include "Networking/NetPacket.hpp"
#include "Networking/NetServer.hpp"
#include "Networking/NetworkManager.hpp"
#include "Scene/ComponentBlob.hpp"
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
    // A registered scene is being edited, not played. The gameplay phases stay off until scene.play
    // turns them on - which is what makes edit mode actually edit mode. Nothing disabled them
    // server-side before, so every scene simulated all the time and hitting Play changed nothing
    // observable; it just produced a second, equally-running scene.
    scene->disable_phases({flecs::PreUpdate, flecs::OnUpdate});

    self.scenes_.emplace_back(
      RegisteredScene{
        .scene = scene,
        .id = static_cast<SceneID>(self.next_scene_id_++),
      }
    );
  }
}

auto Server::unregister_scene(this Server& self, const Scene* scene) -> void {
  std::erase_if(self.scenes_, [scene](const RegisteredScene& r) { return r.scene.get() == scene; });
}

auto Server::tick(this Server& self, const Timestep& timestep) -> void {
  ZoneScoped;

  for (const auto& registered : self.scenes_) {
    registered.scene->runtime_update(timestep);
  }

  // Network last, so a client sees the world as it stands at the end of the tick rather than
  // halfway through it.
  if (self.net_server_ != nullptr) {
    if (self.net_server_->tick(timestep)) {
      self.replicate();
    }

    // No per-tick "are there clients" check here. ENet already raises DISCONNECT_TIMEOUT for a
    // client that died without saying goodbye, and on_client_disconnected handles both cases. A
    // per-tick check instead made the server quit the moment a client stalled longer than the ENet
    // timeout - which the editor does while it compiles shaders.
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

  // What the server offers any client. Not editor commands - a game calls these the same way,
  // and gameplay code or a Lua script registers more with the same call without touching a
  // single engine header. That openness is the whole point; the closed variant this replaced
  // meant a new gameplay action had to be added to the engine itself.
  //
  // Each handler validates its parameters: a malformed call arrives from another process, so
  // reading past the end of `params` has to be impossible rather than unlikely.
  auto scene_of = [&self]() -> Scene* {
    return self.scenes_.empty() ? nullptr : self.scenes_.front().scene.get();
  };

  auto entity_arg = [scene_of](std::span<RPCParameter> params, usize index) -> flecs::entity {
    auto* scene = scene_of();
    if (scene == nullptr || index >= params.size()) {
      return {};
    }

    const auto id = params[index].as_int64();
    if (!id.has_value()) {
      return {};
    }

    // RPCParameter has no u64 alternative, so an entity id crosses as i64 and comes back here.
    auto entity = scene->world.entity(static_cast<flecs::entity_t>(*id));
    return entity.is_valid() ? entity : flecs::entity{};
  };

  auto* net = self.net_server_;

  net->register_proc("entity.destroy", [entity_arg](NetClientID, std::span<RPCParameter> params) {
    if (auto entity = entity_arg(params, 0)) {
      entity.destruct();
    }
  });

  net->register_proc("entity.rename", [entity_arg](NetClientID, std::span<RPCParameter> params) {
    if (params.size() < 2) {
      return;
    }

    if (auto entity = entity_arg(params, 0)) {
      entity.set_name(std::string(params[1].as_str()).c_str());
    }
  });

  net->register_proc("entity.reparent", [entity_arg](NetClientID, std::span<RPCParameter> params) {
    auto entity = entity_arg(params, 0);
    if (!entity) {
      return;
    }

    if (auto parent = entity_arg(params, 1)) {
      entity.child_of(parent);
    } else {
      entity.remove(flecs::ChildOf, flecs::Wildcard);
    }
  });

  net->register_proc("entity.enable", [entity_arg](NetClientID, std::span<RPCParameter> params) {
    if (params.size() < 2) {
      return;
    }

    if (auto entity = entity_arg(params, 0)) {
      const auto enabled = params[1].as_int64().value_or(1) != 0;
      enabled ? entity.enable() : entity.disable();
    }
  });

  net->register_proc("entity.clone", [entity_arg, scene_of](NetClientID, std::span<RPCParameter> params) {
    auto* scene = scene_of();
    auto entity = entity_arg(params, 0);
    if (scene == nullptr || !entity) {
      return;
    }

    auto name = std::string(entity.name().c_str());
    while (scene->world.lookup(name.c_str())) {
      name = fmt::format("{}_clone", name);
    }

    entity.clone(true).set_name(name.c_str());
  });

  net->register_proc("entity.create", [scene_of](NetClientID, std::span<RPCParameter> params) {
    auto* scene = scene_of();
    if (scene == nullptr || params.size() < 2) {
      return;
    }

    auto entity = scene->create_entity(std::string(params[0].as_str()));

    // The archetype is a name, so the client asks for what it wants rather than building it.
    const auto archetype = params[1].as_str();
    if (archetype == "sprite") {
      entity.add<SpriteComponent>();
    } else if (archetype == "camera") {
      entity.add<CameraComponent>();
    } else if (archetype == "light") {
      entity.add<LightComponent>();
    } else if (archetype == "sun") {
      entity.set<LightComponent>({.type = LightComponent::LightType::Directional, .intensity = 10.f})
        .add<AtmosphereComponent>()
        .add<AutoExposureComponent>();
    } else if (archetype == "audio_source") {
      entity.add<AudioSourceComponent>();
    }
  });

  net->register_proc("entity.transform", [entity_arg](NetClientID, std::span<RPCParameter> params) {
    if (params.size() < 11) {
      return;
    }

    auto entity = entity_arg(params, 0);
    if (!entity) {
      return;
    }

    auto f = [&params](const usize i) {
      return params[i].as_f32().value_or(0.f);
    };

    entity.set<TransformComponent>({
      .position = glm::vec3(f(1), f(2), f(3)),
      .rotation = glm::quat(f(4), f(5), f(6), f(7)),
      .scale = glm::vec3(f(8), f(9), f(10)),
    });
  });

  net->register_proc("entity.component", [entity_arg](NetClientID, std::span<RPCParameter> params) {
    if (params.size() < 3) {
      return;
    }

    auto entity = entity_arg(params, 0);
    if (!entity) {
      return;
    }

    const auto component = params[1].as_int64();
    if (!component.has_value()) {
      return;
    }

    // The reflected encoding from ComponentBlob, never a raw byte copy - several components own
    // heap storage or hold raw engine pointers.
    if (!read_component_blob(entity, static_cast<flecs::id_t>(*component), params[2].as_span<u8>())) {
      OX_LOG_WARN("A replicated component did not apply cleanly.");
    }
  });

  net->register_proc("entity.restore", [entity_arg, scene_of](NetClientID, std::span<RPCParameter> params) {
    auto* scene = scene_of();
    if (scene == nullptr || params.size() < 2) {
      return;
    }

    // Rehydrates a subtree the client captured before deleting it, so a delete can be undone.
    const auto json = std::string(params[1].as_str());
    auto parser = simdjson::ondemand::parser{};
    auto padded = simdjson::padded_string(json);
    auto doc = parser.iterate(padded);
    if (doc.error()) {
      OX_LOG_ERROR("entity.restore: could not parse the captured entity.");
      return;
    }

    auto value = doc.get_value();
    if (value.error()) {
      return;
    }

    auto requested_assets = std::vector<UUID>{};
    static_cast<void>(Scene::json_to_entity(*scene, entity_arg(params, 0), value.value_unsafe(), requested_assets));
  });

  net->register_proc("model.spawn", [scene_of](NetClientID, std::span<RPCParameter> params) {
    auto* scene = scene_of();
    if (scene == nullptr || params.empty()) {
      return;
    }

    if (const auto uuid = params[0].as_uuid()) {
      static_cast<void>(scene->create_model_entity(*uuid));
    }
  });

  net->register_proc("scene.create", [&self](NetClientID, std::span<RPCParameter> params) {
    const auto name = params.empty() ? std::string_view("Untitled") : params[0].as_str();
    static_cast<void>(self.create_default_scene(std::string(name)));
  });

  net->register_proc("scene.play", [&self](NetClientID, std::span<RPCParameter> params) {
    if (params.empty()) {
      return;
    }

    const auto id = params[0].as_int64();
    if (!id.has_value()) {
      return;
    }

    const auto scene_id = static_cast<SceneID>(*id);
    const auto it = std::ranges::find_if(self.scenes_, [scene_id](const RegisteredScene& r) {
      return r.id == scene_id;
    });

    if (it == self.scenes_.end()) {
      // Says which ids it does have, because "not found" alone cannot distinguish a stale client
      // mapping from an id that was never assigned the way either side thinks.
      auto held = std::string{};
      for (const auto& r : self.scenes_) {
        held += fmt::format("{} ", static_cast<u64>(r.id));
      }

      OX_LOG_WARN("Asked to play scene {}; this server holds [{}].", *id, held);
      return;
    }

    // Play runs a copy, so stopping returns to the authored world untouched. The copy is a
    // scene like any other, so the client mirrors it through the same path - it does not need
    // to know play mode exists.
    auto copy = Scene::copy(it->scene);
    if (!copy) {
      OX_LOG_ERROR("Could not copy scene {} for play.", *id);
      return;
    }

    self.register_scene(copy);
    self.scenes_.back().playing = true;
    copy->meshes_dirty = true;

    // register_scene left the phases off; this copy is the one that actually runs.
    copy->enable_all_phases();
    copy->runtime_start();
    OX_LOG_INFO("Playing a copy of scene {}.", *id);
  });

  net->register_proc("scene.stop", [&self](NetClientID, std::span<RPCParameter> params) {
    if (params.empty()) {
      return;
    }

    const auto id = params[0].as_int64();
    if (!id.has_value()) {
      return;
    }

    const auto scene_id = static_cast<SceneID>(*id);
    const auto it = std::ranges::find_if(self.scenes_, [scene_id](const RegisteredScene& r) {
      return r.id == scene_id;
    });

    if (it == self.scenes_.end()) {
      OX_LOG_WARN("Asked to stop scene {}, which this server does not have.", *id);
      return;
    }

    it->scene->runtime_stop();

    // Only a play copy is destroyed by stopping - it was made for the run and the scene it came
    // from is untouched. An authored scene is stopped and kept; letting a client delete one by
    // asking it to stop is how the editor lost its world.
    if (it->playing) {
      OX_LOG_INFO("Stopped scene {} and destroyed the play copy.", *id);
      self.unregister_scene(it->scene.get());
    } else {
      it->scene->disable_phases({flecs::PreUpdate, flecs::OnUpdate});
      OX_LOG_INFO("Stopped scene {}; it was not a play copy, so it stays.", *id);
    }
  });

  net->register_proc("scene.destroy", [&self](NetClientID, std::span<RPCParameter> params) {
    if (params.empty()) {
      return;
    }

    const auto id = params[0].as_int64();
    if (!id.has_value()) {
      return;
    }

    const auto scene_id = static_cast<SceneID>(*id);
    const auto it = std::ranges::find_if(self.scenes_, [scene_id](const RegisteredScene& r) {
      return r.id == scene_id;
    });

    if (it != self.scenes_.end()) {
      OX_LOG_INFO("Destroying scene {}.", *id);
      self.unregister_scene(it->scene.get());
    } else {
      OX_LOG_WARN("Asked to destroy scene {}, which this server does not have.", *id);
    }
  });

  net->register_proc("scene.load", [&self](NetClientID, std::span<RPCParameter> params) {
    if (params.empty()) {
      return;
    }

    const auto path = std::string(params[0].as_str());
    if (!path.empty() && !self.load_scene(path)) {
      OX_LOG_ERROR("Could not load the requested scene.");
    }
  });

  net->register_proc("scene.save", [scene_of](NetClientID, std::span<RPCParameter> params) {
    auto* scene = scene_of();
    if (scene == nullptr || params.empty()) {
      return;
    }

    // Serialised on the thread that owns the world, not by a job racing the tick.
    const auto path = std::string(params[0].as_str());
    if (!path.empty() && !scene->save_to_file(path)) {
      OX_LOG_ERROR("Could not save the scene.");
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

  auto
    packet = NetPacket::scene_snapshot(state, registered.builder.current_sequence, registered.id, registered.playing);
  if (!packet.has_value()) {
    OX_LOG_ERROR("Could not serialise a scene snapshot.");
    return;
  }

  // No destroy(): on a successful send ENet takes ownership of the packet and frees it once it
  // has gone out. send_unreliable only destroys it when the send itself fails.
  client->send_unreliable(packet.value());
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
      auto packet = NetPacket::scene_snapshot(
        delta,
        registered.builder.current_sequence,
        registered.id,
        registered.playing
      );
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
