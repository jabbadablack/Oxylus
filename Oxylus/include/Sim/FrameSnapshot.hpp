#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>

#include "Core/Types.hpp"
#include "Core/UUID.hpp"
#include "Scene/DebugDrawList.hpp"
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
  // temporal reprojection belongs to whoever is actually drawing. near_clip, far_clip and fov live
  // in here too - there is no reason to carry a second copy alongside.
  GPU::CameraData camera = {};
};

// One placed mesh, still logical: UUIDs rather than resolved GPU indices, so the client owns the
// mapping from asset identity to device addresses and bindless slots.
struct MeshInstanceSnapshot {
  UUID model_uuid = UUID(nullptr);
  u32 mesh_node_index = 0;
  UUID material_uuid = UUID(nullptr);
  u32 transform_index = 0;
  // Slot index in the scene's mesh instance map, used to match up the dirty list.
  u32 slot_index = 0;
  // Carried so the client can answer "which entity is at this pixel" without reaching into the
  // scene; this is what replaces Scene::transform_index_entities_map.
  EntityHandle entity = EntityHandle::Invalid;
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

  // The full transform array, not just the dirty entries. A client that misses a frame never sees
  // that frame's dirty list, and the renderer's own upload path needs random access for its
  // full-rebuild case; `dirty_transform_ids` is an upload hint, not the whole truth.
  std::vector<GPU::Transforms> transforms = {};
  std::vector<GPU::TransformID> dirty_transform_ids = {};

  // Debug geometry produced by the simulation this frame (bounding boxes, physics shapes). The
  // client merges it with its own before upload.
  std::vector<DebugDrawList::Line> debug_lines = {};
  std::vector<DebugDrawList::Triangle> debug_triangles = {};

  // True when the instance list itself changed shape, so the client must rebuild its GPU arrays
  // rather than patch the dirty entries.
  bool mesh_instances_rebuilt = false;
  std::vector<MeshInstanceSnapshot> mesh_instances = {};
  std::vector<u32> dirty_mesh_instance_slots = {};

  // resize(0) rather than clear-and-shrink: the snapshot is reused every frame and keeping the
  // capacity is what makes steady state allocation-free.
  auto clear(this FrameSnapshot& self) -> void;
};
} // namespace ox
