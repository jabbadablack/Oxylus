#pragma once

#include <filesystem>
#include <span>
#include <variant>

#include "Asset/SamplerDescription.hpp"
#include "Core/Option.hpp"
#include "Core/Types.hpp"

namespace ox {
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
