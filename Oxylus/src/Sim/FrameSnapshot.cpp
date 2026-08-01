#include "Sim/FrameSnapshot.hpp"

#include <algorithm>

namespace ox {
auto FrameSnapshot::clear(this FrameSnapshot& self) -> void {
  ZoneScoped;

  self.frame_id = FrameID::Invalid;
  self.delta_time = 0.f;

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
}

auto FrameSnapshot::find_view(this const FrameSnapshot& self, const SimViewID view_id) -> const ViewSnapshot* {
  const auto it = std::ranges::find_if(self.views, [view_id](const ViewSnapshot& view) {
    return view.view_id == view_id;
  });

  return it == self.views.end() ? nullptr : &*it;
}
} // namespace ox
