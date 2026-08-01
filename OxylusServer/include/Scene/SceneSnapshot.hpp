#pragma once

#include <ankerl/unordered_dense.h>
#include <flecs.h>
#include <vector>

#include "Core/Option.hpp"
#include "Core/Types.hpp"

namespace ox {
struct ComponentState {
  flecs::id_t id = 0;
  u64 hash = 0; // u64_max indicates that this one is a tag
  // TODO: we should probably replace this vector with something pre-allocated
  std::vector<u8> buffer = {};
};

struct EntityState {
  flecs::entity_t entity_id = 0;

  // Identity and hierarchy do not live in any component, so a component-only snapshot loses them
  // entirely - and the hierarchy panel is built on all three.
  std::string name = {};
  flecs::entity_t parent = 0;
  bool enabled = true;

  ankerl::unordered_dense::map<flecs::id_t, ComponentState> components = {};
  ankerl::unordered_dense::set<flecs::id_t> removed_components = {};
};

struct SceneState {
  ankerl::unordered_dense::map<flecs::entity_t, EntityState> entities = {};
  ankerl::unordered_dense::set<flecs::entity_t> removed_entities = {};

  auto clear() -> void {
    entities.clear();
    removed_entities.clear();
  }
};

struct SceneSnapshotBuilder {
  constexpr static auto MAX_SEQUENCES = 32_u8;
  std::array<SceneState, MAX_SEQUENCES> states = {};
  std::array<bool, MAX_SEQUENCES> acks = {};
  u8 current_sequence = 0;

  auto current() -> SceneState& { return states[current_sequence]; }
  auto ack(u8 seq) -> void { acks[seq % MAX_SEQUENCES] = true; }
  auto set_current(this SceneSnapshotBuilder&, SceneState& new_state) -> void;
  auto advance(this SceneSnapshotBuilder&) -> void;
  auto find_last_acked(this SceneSnapshotBuilder& self) -> option<u8>;
  auto delta(this SceneSnapshotBuilder& self) -> SceneState;
  static auto take_snapshot(flecs::world& world, SceneState& state) -> void;
};

// The inverse of take_snapshot: reconstructs the described entities in `world`.
//
// Entities are recreated with the ids they had on the sender, via ecs_make_alive, so a handle means
// the same thing on both sides and commands can name an entity without a translation table.
// Components are decoded through the same reflection take_snapshot encoded them with, never a
// memcpy - several of them own heap storage or hold raw engine pointers.
auto apply_scene_state(flecs::world& world, const SceneState& state) -> void;
} // namespace ox
