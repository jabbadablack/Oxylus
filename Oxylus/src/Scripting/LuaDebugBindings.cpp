#include "Scripting/LuaDebugBindings.hpp"

#include <sol/state.hpp>

// #include "Physics/RayCast.hpp"
// #include "Scene/DebugDrawList.hpp"

namespace ox {
auto DebugBinding::bind(sol::state* state) -> void {
#if 0
  auto debug_table = state->new_usertype<DebugDrawList>("DebugDrawList");
  debug_table.set_function("draw_point", [](DebugDrawList& dl, const glm::vec3& point, glm::vec3 color) -> void {
    dl.draw_point(point, 1.0f, glm::vec4(color, 1.0f));
  });
  debug_table.set_function(
    "draw_line",
    [](DebugDrawList& dl, const glm::vec3& start, const glm::vec3& end, const glm::vec3& color = glm::vec3(1)) -> void {
      dl.draw_line(start, end, 1.0f, glm::vec4(color, 1.0f));
    }
  );
  debug_table.set_function(
    "draw_ray",
    [](DebugDrawList& dl, const RayCast& ray, const glm::vec3& color = glm::vec3(1)) -> void {
      dl.draw_line(ray.get_origin(), ray.get_direction(), 1.0f, glm::vec4(color, 1.0f));
    }
  );
  debug_table.set_function(
    "draw_aabb",
    [](DebugDrawList& dl, const AABB& aabb, const glm::vec3& color, const bool depth_tested) -> void {
      dl.draw_aabb(aabb, glm::vec4(color, 1.0f), false, 1.0f, depth_tested);
    }
  );
#endif
}
} // namespace ox
