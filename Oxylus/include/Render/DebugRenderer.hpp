#pragma once

#include <expected>
#include <vuk/Types.hpp>
#include <vuk/runtime/CommandBuffer.hpp>
#include <vuk/runtime/vk/Allocator.hpp>

#include "Core/Types.hpp"
#include "Scene/DebugDrawList.hpp"

namespace ox {
class Renderer;

// The GPU half of debug drawing: the shared index buffer and the vertex layout. The geometry
// itself lives in DebugDrawList, which knows nothing about vuk - the Scene owns one for
// simulation-side draws, and `draw_list` here is the client's own for editor gizmos and the like.
class DebugRenderer {
public:
  constexpr static auto MODULE_NAME = "DebugRenderer";
  using module_dependencies = std::tuple<Renderer>;

  using Vertex = DebugDrawList::Vertex;
  using Line = DebugDrawList::Line;
  using Point = DebugDrawList::Point;
  using Triangle = DebugDrawList::Triangle;

  static const vuk::Packed vertex_pack;

  static constexpr u32 MAX_LINES = 10'000;
  static constexpr u32 MAX_LINE_VERTICES = MAX_LINES * 2;
  static constexpr u32 MAX_LINE_INDICES = MAX_LINES * 6;

  DebugRenderer() = default;
  ~DebugRenderer() = default;

  auto init() -> std::expected<void, std::string>;
  auto deinit() -> std::expected<void, std::string>;

  const vuk::Unique<vuk::Buffer>& get_global_index_buffer() const { return debug_renderer_context.index_buffer; }

  DebugDrawList draw_list = {};

private:
  struct DebugRendererContext {
    vuk::Unique<vuk::Buffer> index_buffer;
  } debug_renderer_context;
};
} // namespace ox
