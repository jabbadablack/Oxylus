#pragma once

#include <filesystem>
#include <span>
#include <variant>

#include "Core/Option.hpp"
#include "Core/Types.hpp"

namespace ox {
enum class TextureFilter : u8 { Nearest, Linear };
enum class TextureMipFilter : u8 { Nearest, Linear };
enum class TextureAddressMode : u8 { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder };

// How an asset wants to be sampled, in the engine's own vocabulary.
//
// This used to be a vuk::SamplerCreateInfo embedded straight into TextureLoadInfo, which meant a
// plain asset descriptor dragged vuk into every header that named it - including the asset
// manager's, and so into anything that touched assets at all. The renderer converts it at the point
// it actually creates a sampler.
struct SamplerDescription {
  TextureFilter mag_filter = TextureFilter::Linear;
  TextureFilter min_filter = TextureFilter::Linear;
  TextureMipFilter mip_filter = TextureMipFilter::Linear;
  TextureAddressMode address_u = TextureAddressMode::Repeat;
  TextureAddressMode address_v = TextureAddressMode::Repeat;
  TextureAddressMode address_w = TextureAddressMode::Repeat;

  auto operator==(const SamplerDescription& other) const -> bool = default;
};
// Where a texture's bytes come from: a file on disk, or memory the caller already holds.
using TextureDataSource = std::variant<std::filesystem::path, std::span<const u8>>;

// A request to load a texture. Deliberately free of vuk so the asset manager's public API - and
// with it every consumer of assets - stays on the simulation side of the boundary.
struct TextureLoadInfo {
  TextureDataSource source = {};
  u32 level_count = 1;
  bool is_srgb = true;
  option<u32> target_width = nullopt;
  option<u32> target_height = nullopt;
  SamplerDescription sampler = {};
};
} // namespace ox
