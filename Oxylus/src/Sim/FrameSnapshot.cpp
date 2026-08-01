#include "Sim/FrameSnapshot.hpp"

namespace ox {
auto FrameSnapshot::clear(this FrameSnapshot& self) -> void {
  ZoneScoped;

  self.flags = {};
  self.atmosphere = {};
  self.sky = {};
  self.eye_adaptation = {};
  self.post_process = {};
  self.directional_light = {};
  self.tonemap_type = GPU::TonemapType::AgX;

  self.directional_light_cast_shadows = true;
  self.sun_direction_changed = false;
  self.first_clipmap_width = 1.f;
  self.clipmap_selection_bias = 2.f;

  self.views.resize(0);
  self.lights.resize(0);
  self.sprites.resize(0);

  self.debug_lines.resize(0);
  self.debug_triangles.resize(0);

  self.mesh_instances_rebuilt = false;
  self.transforms.resize(0);
  self.dirty_transform_ids.resize(0);
  self.mesh_instances.resize(0);
  self.dirty_mesh_instance_slots.resize(0);
}
} // namespace ox
