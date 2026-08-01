#include "Render/DebugRenderer.hpp"

#include <vuk/vsl/Core.hpp>

#include "Core/App.hpp"
#include "Render/RenderContext.hpp"
#include "Utils/OxMath.hpp"

namespace ox {
const vuk::Packed DebugRenderer::vertex_pack = vuk::Packed{
  vuk::Format::eR32G32B32Sfloat, // 12 vec
  vuk::Format::eR32Uint,         // 4 color
};

auto DebugRenderer::init() -> std::expected<void, std::string> {
  ZoneScoped;

  std::vector<uint32_t> indices = {};
  indices.resize(MAX_LINE_INDICES);

  for (uint32_t i = 0; i < MAX_LINE_INDICES; i++) {
    indices[i] = i;
  }

  auto [i_buff, i_buff_fut] = create_buffer(
    *App::get_rendercontext().superframe_allocator,
    vuk::MemoryUsage::eCPUtoGPU,
    vuk::DomainFlagBits::eTransferOnGraphics,
    std::span(indices)
  );

  auto compiler = vuk::Compiler{};
  i_buff_fut.wait(*App::get_rendercontext().superframe_allocator, compiler);

  debug_renderer_context.index_buffer = std::move(i_buff);

  return {};
}

auto DebugRenderer::deinit() -> std::expected<void, std::string> { return {}; }
} // namespace ox
