#include "Scene/ComponentBlob.hpp"

#include <cstring>

#include "Utils/Log.hpp"

namespace ox {
ComponentBlobWriter::ComponentBlobWriter(flecs::world& world_, std::vector<u8>& out_)
    : IEntitySerializer(world_),
      out(out_) {}

auto ComponentBlobWriter::write_bytes(this ComponentBlobWriter& self, const void* data, const usize size) -> void {
  const auto offset = self.out.size();
  self.out.resize(offset + size);
  std::memcpy(self.out.data() + offset, data, size);
}

auto ComponentBlobWriter::write_string(this ComponentBlobWriter& self, const c8* str) -> void {
  const auto length = static_cast<u32>(str ? std::strlen(str) : 0);
  self.write_bytes(&length, sizeof(length));
  if (length > 0) {
    self.write_bytes(str, length);
  }
}

auto ComponentBlobWriter::on_primitive(std::string_view, const Primitive primitive) -> void {
  std::visit(
    [this](auto* value) {
      if (value != nullptr) {
        this->write_bytes(value, sizeof(*value));
      }
    },
    primitive
  );
}

auto ComponentBlobWriter::on_string(std::string_view, const c8** str) -> void { write_string(str ? *str : nullptr); }

auto ComponentBlobWriter::on_entity(std::string_view, flecs::entity* entity) -> void {
  const auto id = entity ? entity->id() : 0_u64;
  write_bytes(&id, sizeof(id));
}

auto ComponentBlobWriter::on_enum(
  std::string_view, const ecs_meta_op_kind_t underlying_kind, flecs::entity_t, void* ptr
) -> void {
  switch (underlying_kind) {
    case EcsOpU8:
    case EcsOpI8 : write_bytes(ptr, 1); break;
    case EcsOpU16:
    case EcsOpI16: write_bytes(ptr, 2); break;
    case EcsOpU64:
    case EcsOpI64: write_bytes(ptr, 8); break;
    default      : write_bytes(ptr, 4); break;
  }
}

auto ComponentBlobWriter::on_component(std::string_view, flecs::id_t* component) -> void {
  const auto id = component ? *component : 0_u64;
  write_bytes(&id, sizeof(id));
}

auto ComponentBlobWriter::on_struct(std::string_view, flecs::meta::op_t* ops, const i32 op_count, void* base) -> void {
  serialize_ops(ops + 1, op_count - 1, base);
}

auto ComponentBlobWriter::on_opaque_value(
  std::string_view, flecs::entity_t, void*, const flecs::entity_t opaque_type, const void* value
) -> void {
  // std::string and UUID are the only opaque types the engine registers, both as flecs::String.
  if (opaque_type == flecs::String) {
    write_string(*static_cast<const c8* const*>(value));
    return;
  }

  OX_LOG_WARN("Cannot serialise opaque component field of an unsupported type.");
}

ComponentBlobReader::ComponentBlobReader(flecs::world& world_, const std::span<const u8> in_)
    : IEntitySerializer(world_),
      in(in_) {}

auto ComponentBlobReader::read_bytes(this ComponentBlobReader& self, void* data, const usize size) -> void {
  if (self.cursor + size > self.in.size()) {
    self.overflowed = true;
    std::memset(data, 0, size);
    return;
  }

  std::memcpy(data, self.in.data() + self.cursor, size);
  self.cursor += size;
}

auto ComponentBlobReader::read_string(this ComponentBlobReader& self) -> std::string {
  auto length = 0_u32;
  self.read_bytes(&length, sizeof(length));
  if (length == 0 || self.cursor + length > self.in.size()) {
    if (length != 0) {
      self.overflowed = true;
    }
    return {};
  }

  auto value = std::string(reinterpret_cast<const c8*>(self.in.data() + self.cursor), length);
  self.cursor += length;
  return value;
}

auto ComponentBlobReader::on_primitive(std::string_view, const Primitive primitive) -> void {
  std::visit(
    [this](auto* value) {
      if (value != nullptr) {
        this->read_bytes(value, sizeof(*value));
      }
    },
    primitive
  );
}

auto ComponentBlobReader::on_string(std::string_view, const c8** str) -> void {
  // Plain `const char*` fields are not owned by the component, so there is nowhere to put a decoded
  // string. Consume the bytes to stay in step and leave the field alone; the engine's string-like
  // fields are std::string and UUID, which arrive through the opaque path instead.
  const auto value = read_string();
  static_cast<void>(str);
  static_cast<void>(value);
}

auto ComponentBlobReader::on_entity(std::string_view, flecs::entity* entity) -> void {
  auto id = 0_u64;
  read_bytes(&id, sizeof(id));
  if (entity != nullptr) {
    *entity = flecs::entity(world, id);
  }
}

auto ComponentBlobReader::on_enum(
  std::string_view, const ecs_meta_op_kind_t underlying_kind, flecs::entity_t, void* ptr
) -> void {
  switch (underlying_kind) {
    case EcsOpU8:
    case EcsOpI8 : read_bytes(ptr, 1); break;
    case EcsOpU16:
    case EcsOpI16: read_bytes(ptr, 2); break;
    case EcsOpU64:
    case EcsOpI64: read_bytes(ptr, 8); break;
    default      : read_bytes(ptr, 4); break;
  }
}

auto ComponentBlobReader::on_component(std::string_view, flecs::id_t* component) -> void {
  auto id = 0_u64;
  read_bytes(&id, sizeof(id));
  if (component != nullptr) {
    *component = id;
  }
}

auto ComponentBlobReader::on_struct(std::string_view, flecs::meta::op_t* ops, const i32 op_count, void* base) -> void {
  serialize_ops(ops + 1, op_count - 1, base);
}

auto ComponentBlobReader::on_opaque_field(std::string_view, const flecs::entity_t field_type, void* field_ptr) -> void {
  const auto value = read_string();

  const auto* opaque = ecs_get(world, field_type, EcsOpaque);
  if (opaque == nullptr || opaque->assign_string == nullptr || field_ptr == nullptr) {
    OX_LOG_WARN("Cannot deserialise opaque component field: no string assignment available.");
    return;
  }

  opaque->assign_string(field_ptr, value.c_str());
}

auto write_component_blob(flecs::entity entity, const flecs::id_t component, std::vector<u8>& out) -> bool {
  ZoneScoped;

  auto world = entity.world();
  const auto component_entity = flecs::entity(world, component);
  if (!component_entity.has<flecs::Component>()) {
    return false;
  }

  void* data = entity.get_mut(component);
  if (data == nullptr) {
    return false;
  }

  auto writer = ComponentBlobWriter(world, out);
  writer.serialize(component_entity, data);

  return true;
}

auto read_component_blob(flecs::entity entity, const flecs::id_t component, const std::span<const u8> in) -> bool {
  ZoneScoped;

  auto world = entity.world();
  const auto component_entity = flecs::entity(world, component);
  if (!component_entity.has<flecs::Component>()) {
    return false;
  }

  if (!entity.has(component)) {
    entity.add(component);
  }

  void* data = entity.get_mut(component);
  if (data == nullptr) {
    return false;
  }

  auto reader = ComponentBlobReader(world, in);
  reader.serialize(component_entity, data);
  entity.modified(component);

  return !reader.overflowed;
}
} // namespace ox
