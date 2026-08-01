#include "Scene/SceneSnapshot.hpp"

#include <fmt/base.h>

#include "Scene/ComponentBlob.hpp"
#include "Scene/Components.hpp"
#include "Utils/Log.hpp"

namespace ox {
auto SceneSnapshotBuilder::set_current(this SceneSnapshotBuilder& self, SceneState& new_state) -> void {
  ZoneScoped;

  auto& current = self.current();
  current.clear();
  current = new_state; // There is gotta be a way to optimize this copy
}

auto SceneSnapshotBuilder::advance(this SceneSnapshotBuilder& self) -> void {
  ZoneScoped;

  self.current_sequence = (self.current_sequence + 1) % MAX_SEQUENCES;
  self.states[self.current_sequence].clear();
  self.acks[self.current_sequence] = false;
}

auto SceneSnapshotBuilder::find_last_acked(this SceneSnapshotBuilder& self) -> option<u8> {
  ZoneScoped;

  for (auto i = 1_u8; i < MAX_SEQUENCES; i++) {
    auto seq = (self.current_sequence + MAX_SEQUENCES - 1) % MAX_SEQUENCES;
    if (self.acks[seq]) {
      return seq;
    }
  }

  return nullopt;
}

auto SceneSnapshotBuilder::delta(this SceneSnapshotBuilder& self) -> SceneState {
  ZoneScoped;

  auto delta = SceneState{};
  const auto& current_state = self.states[self.current_sequence];
  auto last_acked = self.find_last_acked();
  if (!last_acked.has_value()) {
    return current_state;
  }

  const auto& last_state = self.states[last_acked.value()];
  // check for changed entities
  for (const auto& [entity_id, entity_state] : current_state.entities) {
    auto last_it = last_state.entities.find(entity_id);
    if (last_it != last_state.entities.end()) {
      // this entity exist beteen current sequence and last sequence
      const auto& [last_entity_id, last_entity_state] = *last_it;
      auto delta_entity = EntityState{
        .entity_id = entity_id,
        .name = entity_state.name,
        .parent = entity_state.parent,
        .enabled = entity_state.enabled,
      };

      // A rename, a reparent or a disable changes nothing about the components, so identity
      // has to be diffed separately or those edits never reach the client.
      auto changed = entity_state.name != last_entity_state.name || entity_state.parent != last_entity_state.parent ||
                     entity_state.enabled != last_entity_state.enabled;
      // check for changed components
      for (const auto& [component_id, component_state] : entity_state.components) {
        auto prev_component_it = last_entity_state.components.find(component_id);
        if (
          prev_component_it == last_entity_state.components.end() ||
          prev_component_it->second.hash != component_state.hash
        ) {
          delta_entity.components.emplace(component_id, component_state);
          changed = true; // we've inserted new/changed component
        }
      }

      // check for removed components
      for (const auto& [component_id, _] : last_entity_state.components) {
        if (!entity_state.components.contains(component_id)) {
          delta_entity.removed_components.emplace(component_id);
          changed = true; // the component has been removed in current seq.
        }
      }

      if (changed) {
        delta.entities.emplace(entity_id, std::move(delta_entity));
      }
    } else {
      // new entity
      delta.entities.emplace(entity_id, entity_state);
    }
  }

  // check for removed entities
  for (const auto& [entity_id, _] : last_state.entities) {
    if (!current_state.entities.contains(entity_id)) {
      delta.removed_entities.insert(entity_id);
    }
  }

  return delta;
}

auto SceneSnapshotBuilder::take_snapshot(flecs::world& world, SceneState& state) -> void {
  ZoneScoped;

  world.query_builder()
    .with<Networked>() //
    .each([&](flecs::entity component) {
      auto component_id = component.raw_id();
      auto is_component = component.has<flecs::Component>();
      auto component_info = flecs::Component{};
      if (is_component) {
        component_info = component.get<flecs::Component>();
      }

      // EcsQueryMatchDisabled, because flecs excludes disabled entities from queries by default.
      // Without it a disabled entity vanishes from the snapshot entirely - and, far worse, a delta
      // would then list it under removed_entities and the client would destroy it.
      world.query_builder().with(component).query_flags(EcsQueryMatchDisabled).each([&](flecs::entity entity) {
        auto entity_id = entity.id();
        auto component_state = ComponentState{.id = component_id, .hash = ~0_u64};
        if (is_component) {
          // Through the reflection, not a memcpy of the raw bytes: several components own heap
          // storage or raw engine pointers, and the reflection binds only the authoritative
          // fields. The hash is taken over the encoded form so it tracks what actually shipped.
          if (write_component_blob(entity, component_id, component_state.buffer)) {
            component_state.hash = ankerl::unordered_dense::detail::wyhash::hash(
              component_state.buffer.data(),
              component_state.buffer.size()
            );
          }
        }

        auto& entity_state = state.entities[entity_id];
        entity_state.entity_id = entity_id;
        entity_state.name = entity.name().c_str();
        entity_state.parent = entity.parent().id();
        entity_state.enabled = !entity.has(flecs::Disabled);
        entity_state.components.emplace(component_id, std::move(component_state));
      });
    });
}

auto apply_scene_state(flecs::world& world, const SceneState& state) -> void {
  ZoneScoped;

  // Removals first: an entity destroyed on the sender must not survive here just because it also
  // appears in an older part of the same delta.
  for (const auto entity_id : state.removed_entities) {
    if (world.is_alive(entity_id)) {
      world.entity(entity_id).destruct();
    }
  }

  // Two passes: every entity has to exist before any parent link is set, or an entity that arrives
  // before its parent would be reparented to something not yet created.
  for (const auto& [entity_id, entity_state] : state.entities) {
    if (!world.is_alive(entity_id)) {
      // The sender's id, not a fresh one. That is what lets a handle mean the same thing on both
      // sides; ecs_make_alive is the only way to ask flecs for a specific id.
      ecs_make_alive(world, entity_id);
    }
  }

  for (const auto& [entity_id, entity_state] : state.entities) {
    auto entity = world.entity(entity_id);

    if (!entity_state.name.empty() && entity.name() != entity_state.name.c_str()) {
      entity.set_name(entity_state.name.c_str());
    }

    const auto parent = entity_state.parent;
    if (parent != 0 && world.is_alive(parent)) {
      if (entity.parent().id() != parent) {
        entity.child_of(world.entity(parent));
      }
    } else if (parent == 0 && entity.parent().id() != 0) {
      entity.remove(flecs::ChildOf, flecs::Wildcard);
    }

    if (entity_state.enabled) {
      entity.enable();
    } else {
      entity.disable();
    }

    for (const auto& component_id : entity_state.removed_components) {
      if (entity.has(component_id)) {
        entity.remove(component_id);
      }
    }

    for (const auto& [component_id, component_state] : entity_state.components) {
      // A tag carries no data, so there is nothing to decode - just make sure it is present.
      if (component_state.hash == ~0_u64 && component_state.buffer.empty()) {
        entity.add(component_id);
        continue;
      }

      if (!read_component_blob(entity, component_id, component_state.buffer)) {
        OX_LOG_WARN("Replicated component {} did not apply cleanly to entity {}.", component_id, entity_id);
      }
    }
  }
}

} // namespace ox
