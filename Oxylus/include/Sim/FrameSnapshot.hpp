#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>

#include "Core/Types.hpp"
#include "Core/UUID.hpp"
#include "Scene/SceneGPU.hpp"
#include "Sim/Fwd.hpp"

namespace ox {
// What the client asks the simulation to render, for one viewport.
struct ViewRequest {
  SimViewID view_id = SimViewID::Invalid;
  SimCameraSource source = SimCameraSource::ClientSupplied;

  // Only when source == SimEntity.
  EntityHandle camera_entity = EntityHandle::Invalid;

  glm::uvec2 viewport_size = {};
  glm::uvec2 viewport_offset = {};

  // Only when source == ClientSupplied. previous_* and the jitter history are left zeroed: they are
  // the renderer's own temporal state, not the simulation's.
  GPU::CameraData camera = {};
};

// A resolved view for one frame.
struct ViewSnapshot {
  SimViewID view_id = SimViewID::Invalid;
  SimCameraSource source = SimCameraSource::Invalid;
  EntityHandle camera_entity = EntityHandle::Invalid;

  glm::uvec2 viewport_size = {};
  glm::uvec2 viewport_offset = {};

  // Current-frame matrices only. The renderer fills previous_* from its own history, because
  // temporal reprojection belongs to whoever is actually drawing.
  GPU::CameraData camera = {};

  f32 near_clip = 0.f;
  f32 far_clip = 0.f;
  f32 fov = 0.f;
};

// A 2D draw, still logical: the material is a UUID and the depth ordering is left to the client,
// because distance depends on which view is being drawn.
struct Sprite2DSnapshot {
  u16 flags = 0;
  f32 position_y = 0.f;
  f32 position_z = 0.f;
  u32 transform_index = 0;
  UUID material_uuid = UUID(nullptr);
};

// One frame of simulation state, in the form the renderer needs it. Owning and view-independent:
// anything that depends on which viewport is being drawn is computed client-side from this.
//
// This replaces the eight live ECS queries RendererInstance::update used to run every frame, and is
// the payload that will eventually cross a real transport.
struct FrameSnapshot {
  FrameID frame_id = FrameID::Invalid;
  f32 delta_time = 0.f;

  GPU::SceneFlags flags = {};

  // Filled from the scene's components. The renderer overlays its own LUT extents onto
  // `atmosphere` before upload - those describe its textures, not the world.
  GPU::Atmosphere atmosphere = {};
  GPU::SkyData sky = {};
  GPU::EyeAdaptationSettings eye_adaptation = {};
  GPU::PostProcessSettings post_process = {};
  GPU::DirectionalLight directional_light = {};
  GPU::TonemapType tonemap_type = GPU::TonemapType::AgX;

  bool directional_light_cast_shadows = true;
  bool sun_direction_changed = false;
  f32 first_clipmap_width = 1.f;
  f32 clipmap_selection_bias = 2.f;

  std::vector<ViewSnapshot> views = {};
  std::vector<GPU::Light> lights = {};
  std::vector<Sprite2DSnapshot> sprites = {};

  // resize(0) rather than clear-and-shrink: the snapshot is reused every frame and keeping the
  // capacity is what makes steady state allocation-free.
  auto clear(this FrameSnapshot& self) -> void;

  auto find_view(this const FrameSnapshot& self, SimViewID view_id) -> const ViewSnapshot*;
};
} // namespace ox
