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
      }
    },
    command.payload
  );

  return result;
}
} // namespace ox
