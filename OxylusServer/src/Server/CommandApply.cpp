#include <array>
#include <cstring>
#include <fmt/format.h>

#include "Scene/ComponentBlob.hpp"
#include "Scene/Scene.hpp"
#include "Server/ServerCommand.hpp"
#include "Utils/Log.hpp"

namespace ox {
auto to_entity(Scene& scene, const EntityHandle handle) -> flecs::entity {
  if (handle == EntityHandle::Invalid) {
    return {};
  }

  auto entity = scene.world.entity(static_cast<flecs::entity_t>(handle));
  return entity.is_valid() ? entity : flecs::entity{};
}

// A command is a tag byte for the variant alternative followed by its fields. Fixed-width fields
// are written raw; the two variable ones (a JSON string, a component blob) are length-prefixed.
auto write_bytes(std::vector<u8>& out, const void* data, const usize size) -> void {
  const auto offset = out.size();
  out.resize(offset + size);
  std::memcpy(out.data() + offset, data, size);
}

template <typename T>
auto write_pod(std::vector<u8>& out, const T& value) -> void {
  write_bytes(out, &value, sizeof(T));
}

auto write_blob(std::vector<u8>& out, const void* data, const usize size) -> void {
  write_pod(out, static_cast<u64>(size));
  if (size > 0) {
    write_bytes(out, data, size);
  }
}

struct CommandReader {
  std::span<const u8> bytes = {};
  usize cursor = 0;
  bool overflowed = false;

  auto read_bytes(this CommandReader& self, void* out, const usize size) -> void {
    if (self.cursor + size > self.bytes.size()) {
      self.overflowed = true;
      std::memset(out, 0, size);
      return;
    }

    std::memcpy(out, self.bytes.data() + self.cursor, size);
    self.cursor += size;
  }

  template <typename T>
  auto read_pod(this CommandReader& self) -> T {
    auto value = T{};
    self.read_bytes(&value, sizeof(T));
    return value;
  }

  auto read_blob(this CommandReader& self) -> std::vector<u8> {
    const auto size = static_cast<usize>(self.read_pod<u64>());
    if (size == 0 || self.cursor + size > self.bytes.size()) {
      self.overflowed = self.overflowed || size != 0;
      return {};
    }

    auto value = std::vector<u8>(
      self.bytes.begin() + static_cast<std::ptrdiff_t>(self.cursor),
      self.bytes.begin() + static_cast<std::ptrdiff_t>(self.cursor + size)
    );
    self.cursor += size;
    return value;
  }
};

auto serialize_command(const ServerCommand& command) -> std::vector<u8> {
  ZoneScoped;

  auto out = std::vector<u8>{};
  write_pod(out, static_cast<u8>(command.payload.index()));

  std::visit(
    [&out]<typename T>(const T& cmd) {
      if constexpr (std::is_same_v<T, CmdDestroyEntity>) {
        write_pod(out, cmd.entity);

      } else if constexpr (std::is_same_v<T, CmdRestoreEntity>) {
        write_pod(out, cmd.parent);
        write_blob(out, cmd.serialized_json.data(), cmd.serialized_json.size());

      } else if constexpr (std::is_same_v<T, CmdSetComponent>) {
        write_pod(out, cmd.entity);
        write_pod(out, cmd.state.id);
        write_pod(out, cmd.state.hash);
        write_blob(out, cmd.state.buffer.data(), cmd.state.buffer.size());

      } else if constexpr (std::is_same_v<T, CmdSetTransform>) {
        write_pod(out, cmd.entity);
        write_pod(out, cmd.position);
        write_pod(out, cmd.rotation);
        write_pod(out, cmd.scale);

      } else if constexpr (std::is_same_v<T, CmdRenameEntity>) {
        write_pod(out, cmd.entity);
        write_blob(out, cmd.name.data(), cmd.name.size());

      } else if constexpr (std::is_same_v<T, CmdSetEntityEnabled>) {
        write_pod(out, cmd.entity);
        write_pod(out, static_cast<u8>(cmd.enabled ? 1 : 0));

      } else if constexpr (std::is_same_v<T, CmdReparentEntity>) {
        write_pod(out, cmd.entity);
        write_pod(out, cmd.parent);

      } else if constexpr (std::is_same_v<T, CmdCloneEntity>) {
        write_pod(out, cmd.entity);

      } else if constexpr (std::is_same_v<T, CmdCreateEntity>) {
        write_blob(out, cmd.name.data(), cmd.name.size());
        write_blob(out, cmd.archetype.data(), cmd.archetype.size());

      } else if constexpr (std::is_same_v<T, CmdSpawnModel>) {
        // As its string form: UUID has no byte-wise constructor, and from_string is the public way
        // back. A model spawn is rare enough that the extra bytes do not matter.
        const auto uuid_str = cmd.model_uuid.str();
        write_blob(out, uuid_str.data(), uuid_str.size());

      } else if constexpr (std::is_same_v<T, CmdSaveScene>) {
        write_blob(out, cmd.path.data(), cmd.path.size());
      }
    },
    command.payload
  );

  return out;
}

auto deserialize_command(const std::span<const u8> bytes) -> option<ServerCommand> {
  ZoneScoped;

  auto reader = CommandReader{.bytes = bytes};
  const auto tag = reader.read_pod<u8>();

  auto command = ServerCommand{};
  switch (tag) {
    case 0: {
      command.payload = CmdDestroyEntity{.entity = reader.read_pod<EntityHandle>()};
    } break;
    case 1: {
      auto cmd = CmdRestoreEntity{};
      cmd.parent = reader.read_pod<EntityHandle>();
      const auto json = reader.read_blob();
      cmd.serialized_json.assign(json.begin(), json.end());
      command.payload = std::move(cmd);
    } break;
    case 2: {
      auto cmd = CmdSetComponent{};
      cmd.entity = reader.read_pod<EntityHandle>();
      cmd.state.id = reader.read_pod<flecs::id_t>();
      cmd.state.hash = reader.read_pod<u64>();
      cmd.state.buffer = reader.read_blob();
      command.payload = std::move(cmd);
    } break;
    case 3: {
      auto cmd = CmdSetTransform{};
      cmd.entity = reader.read_pod<EntityHandle>();
      cmd.position = reader.read_pod<glm::vec3>();
      cmd.rotation = reader.read_pod<glm::quat>();
      cmd.scale = reader.read_pod<glm::vec3>();
      command.payload = std::move(cmd);
    } break;
    case 4: {
      auto cmd = CmdRenameEntity{};
      cmd.entity = reader.read_pod<EntityHandle>();
      const auto name = reader.read_blob();
      cmd.name.assign(name.begin(), name.end());
      command.payload = std::move(cmd);
    } break;
    case 5: {
      auto cmd = CmdSetEntityEnabled{};
      cmd.entity = reader.read_pod<EntityHandle>();
      cmd.enabled = reader.read_pod<u8>() != 0;
      command.payload = cmd;
    } break;
    case 6: {
      auto cmd = CmdReparentEntity{};
      cmd.entity = reader.read_pod<EntityHandle>();
      cmd.parent = reader.read_pod<EntityHandle>();
      command.payload = cmd;
    } break;
    case 7: {
      command.payload = CmdCloneEntity{.entity = reader.read_pod<EntityHandle>()};
    } break;
    case 8: {
      auto cmd = CmdCreateEntity{};
      const auto name = reader.read_blob();
      cmd.name.assign(name.begin(), name.end());
      const auto archetype = reader.read_blob();
      cmd.archetype.assign(archetype.begin(), archetype.end());
      command.payload = std::move(cmd);
    } break;
    case 9: {
      const auto uuid_bytes = reader.read_blob();
      const auto uuid_str = std::string(uuid_bytes.begin(), uuid_bytes.end());
      auto uuid = UUID::from_string(uuid_str);
      if (!uuid.has_value()) {
        OX_LOG_ERROR("Received a model spawn with a malformed uuid.");
        return nullopt;
      }

      command.payload = CmdSpawnModel{.model_uuid = uuid.value()};
    } break;
    case 10: {
      auto cmd = CmdSaveScene{};
      const auto path = reader.read_blob();
      cmd.path.assign(path.begin(), path.end());
      command.payload = std::move(cmd);
    } break;
    default: {
      OX_LOG_ERROR("Received a command with an unknown tag {}.", tag);
      return nullopt;
    }
  }

  if (reader.overflowed) {
    OX_LOG_ERROR("Received a truncated command.");
    return nullopt;
  }

  return command;
}

auto apply_command(Scene& scene, const ServerCommand& command) -> EntityHandle {
  ZoneScoped;

  auto result = EntityHandle::Invalid;

  std::visit(
    [&scene, &result]<typename T>(const T& cmd) {
      if constexpr (std::is_same_v<T, CmdDestroyEntity>) {
        if (auto entity = to_entity(scene, cmd.entity)) {
          entity.destruct();
        }

      } else if constexpr (std::is_same_v<T, CmdRestoreEntity>) {
        auto parser = simdjson::ondemand::parser{};
        auto padded = simdjson::padded_string(cmd.serialized_json);
        auto doc = parser.iterate(padded);
        if (doc.error()) {
          OX_LOG_ERROR("CmdRestoreEntity: could not parse the captured entity.");
          return;
        }

        auto value = doc.get_value();
        if (value.error()) {
          OX_LOG_ERROR("CmdRestoreEntity: captured entity is not a JSON value.");
          return;
        }

        auto requested_assets = std::vector<UUID>{};
        auto
          restored = Scene::json_to_entity(scene, to_entity(scene, cmd.parent), value.value_unsafe(), requested_assets);
        result = static_cast<EntityHandle>(restored.id());

      } else if constexpr (std::is_same_v<T, CmdSetComponent>) {
        if (auto entity = to_entity(scene, cmd.entity)) {
          if (!read_component_blob(entity, cmd.state.id, cmd.state.buffer)) {
            OX_LOG_WARN("CmdSetComponent: component blob did not apply cleanly.");
          }
        }

      } else if constexpr (std::is_same_v<T, CmdSetTransform>) {
        if (auto entity = to_entity(scene, cmd.entity)) {
          entity.set<TransformComponent>({
            .position = cmd.position,
            .rotation = cmd.rotation,
            .scale = cmd.scale,
          });
        }

      } else if constexpr (std::is_same_v<T, CmdRenameEntity>) {
        if (auto entity = to_entity(scene, cmd.entity)) {
          entity.set_name(cmd.name.c_str());
        }

      } else if constexpr (std::is_same_v<T, CmdSetEntityEnabled>) {
        if (auto entity = to_entity(scene, cmd.entity)) {
          cmd.enabled ? entity.enable() : entity.disable();
        }

      } else if constexpr (std::is_same_v<T, CmdReparentEntity>) {
        if (auto entity = to_entity(scene, cmd.entity)) {
          if (auto parent = to_entity(scene, cmd.parent)) {
            entity.child_of(parent);
          } else {
            entity.remove(flecs::ChildOf, flecs::Wildcard);
          }
        }

      } else if constexpr (std::is_same_v<T, CmdCloneEntity>) {
        if (auto entity = to_entity(scene, cmd.entity)) {
          auto name = std::string(entity.name().c_str());
          while (scene.world.lookup(name.c_str())) {
            name = fmt::format("{}_clone", name);
          }

          auto cloned = entity.clone(true);
          cloned.set_name(name.c_str());
          result = static_cast<EntityHandle>(cloned.id());
        }

      } else if constexpr (std::is_same_v<T, CmdCreateEntity>) {
        auto entity = scene.create_entity(cmd.name);

        // The archetypes the create menu offers, interpreted here so the client only has to name
        // what it wants rather than build it.
        if (cmd.archetype == "sprite") {
          entity.add<SpriteComponent>();
        } else if (cmd.archetype == "camera") {
          entity.add<CameraComponent>();
        } else if (cmd.archetype == "light") {
          entity.add<LightComponent>();
        } else if (cmd.archetype == "sun") {
          entity.set<LightComponent>({.type = LightComponent::LightType::Directional, .intensity = 10.f})
            .add<AtmosphereComponent>()
            .add<AutoExposureComponent>();
        } else if (cmd.archetype == "audio_source") {
          entity.add<AudioSourceComponent>();
        }

        result = static_cast<EntityHandle>(entity.id());

      } else if constexpr (std::is_same_v<T, CmdSpawnModel>) {
        result = static_cast<EntityHandle>(scene.create_model_entity(cmd.model_uuid).id());

      } else if constexpr (std::is_same_v<T, CmdSaveScene>) {
        // Serialised on the thread that owns the world, not by a job racing the tick.
        if (!scene.save_to_file(cmd.path)) {
          OX_LOG_ERROR("Could not save the scene to \"{}\".", cmd.path);
        }
      }
    },
    command.payload
  );

  return result;
}
} // namespace ox
