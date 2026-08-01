#pragma once

#include <span>
#include <vector>

#include "Core/Types.hpp"
#include "Scene/EntitySerializer.hpp"

namespace ox {
// Serialises a component through its reflection rather than by copying its bytes.
//
// The bytes are not safe to copy. ParticleSystemComponent holds a std::vector, RigidBodyComponent
// and CharacterControllerComponent hold raw engine pointers, and several components carry derived
// state (world_aabb, sprite rects, camera matrices) that must not be replicated. The reflection in
// Components.cpp already encodes exactly that distinction - it binds only the authoritative fields -
// so walking it is both safer and more correct than a memcpy.
//
// The format is positional: writer and reader walk the same meta ops in the same order, so no field
// names or tags are stored. It is only valid between builds that agree on the reflection, which is
// the same constraint the rest of the snapshot machinery already has.
struct ComponentBlobWriter final : IEntitySerializer {
  std::vector<u8>& out;

  ComponentBlobWriter(flecs::world& world_, std::vector<u8>& out_);

  auto on_primitive(std::string_view name, Primitive primitive) -> void override;
  auto on_string(std::string_view name, const c8** str) -> void override;
  auto on_entity(std::string_view name, flecs::entity* entity) -> void override;
  auto on_enum(std::string_view name, ecs_meta_op_kind_t underlying_kind, flecs::entity_t type, void* ptr)
    -> void override;
  auto on_component(std::string_view name, flecs::id_t* component) -> void override;
  auto on_struct(std::string_view name, flecs::meta::op_t* ops, i32 op_count, void* base) -> void override;
  auto on_opaque_value(
    std::string_view name, flecs::entity_t field_type, void* field_ptr, flecs::entity_t opaque_type, const void* value
  ) -> void override;

  auto write_bytes(this ComponentBlobWriter& self, const void* data, usize size) -> void;
  auto write_string(this ComponentBlobWriter& self, const c8* str) -> void;
};

struct ComponentBlobReader final : IEntitySerializer {
  std::span<const u8> in;
  usize cursor = 0;
  bool overflowed = false;

  ComponentBlobReader(flecs::world& world_, std::span<const u8> in_);

  auto on_primitive(std::string_view name, Primitive primitive) -> void override;
  auto on_string(std::string_view name, const c8** str) -> void override;
  auto on_entity(std::string_view name, flecs::entity* entity) -> void override;
  auto on_enum(std::string_view name, ecs_meta_op_kind_t underlying_kind, flecs::entity_t type, void* ptr)
    -> void override;
  auto on_component(std::string_view name, flecs::id_t* component) -> void override;
  auto on_struct(std::string_view name, flecs::meta::op_t* ops, i32 op_count, void* base) -> void override;

  auto assigns_opaque() const -> bool override { return true; }
  auto on_opaque_field(std::string_view name, flecs::entity_t field_type, void* field_ptr) -> void override;

  auto read_bytes(this ComponentBlobReader& self, void* data, usize size) -> void;
  auto read_string(this ComponentBlobReader& self) -> std::string;
};

// Writes `component` on `entity` into `out`. Returns false for tags and for components with no
// reflection, where there is nothing to serialise.
auto write_component_blob(flecs::entity entity, flecs::id_t component, std::vector<u8>& out) -> bool;

// Applies a blob back onto `entity`, adding the component if it is not already there.
auto read_component_blob(flecs::entity entity, flecs::id_t component, std::span<const u8> in) -> bool;
} // namespace ox
