#pragma once

#include <algorithm>
#include <glm/gtc/packing.hpp>
#include <vector>
#include <vuk/Types.hpp>

#include "Core/Types.hpp"
#include "Scene/SceneGPU.hpp"
#include "Utils/OxMath.hpp"

namespace ox::GPU {
struct DrawBatch2D {
  vuk::Name pipeline_name = {};
  u32 offset = 0;
  u32 count = 0;
};

struct RenderQueue2D {
  std::vector<DrawBatch2D> batches = {};
  std::vector<SpriteGPUData> sprite_data = {};

  u32 num_sprites = 0;
  u32 previous_offset = 0;

  u32 last_batches_size = 0;
  u32 last_sprite_data_size = 0;

  void init() {
    clear();
    batches.reserve(last_batches_size);
    sprite_data.reserve(last_sprite_data_size);
    batches.emplace_back(DrawBatch2D{.pipeline_name = "2d_forward", .offset = previous_offset, .count = 0});
  }

  void update() {
    if (!batches.empty()) {
      batches.back().count = num_sprites - batches.back().offset;
    }
    previous_offset = num_sprites;
  }

  void add(u16 render_flags, f32 position_y, u32 transform_id, u32 material_id, f32 distance) {
    const u32 flags_and_distance = math::pack_u16(render_flags, glm::packHalf1x16(distance));
    const u32 materialid_and_ypos = math::pack_u16(static_cast<u16>(material_id), glm::packHalf1x16(position_y));

    sprite_data.emplace_back(
      SpriteGPUData{
        .material_id16_ypos16 = materialid_and_ypos,
        .flags16_distance16 = flags_and_distance,
        .transform_id = transform_id,
      }
    );

    num_sprites += 1;
  }

  void sort() { std::ranges::sort(sprite_data, std::greater<SpriteGPUData>()); }

  void clear() {
    num_sprites = 0;
    previous_offset = 0;
    last_batches_size = static_cast<u32>(batches.size());
    last_sprite_data_size = static_cast<u32>(sprite_data.size());

    batches.clear();
    sprite_data.clear();
  }
};
} // namespace ox::GPU
