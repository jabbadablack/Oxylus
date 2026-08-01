#include "Scene/DebugDrawList.hpp"

#include <array>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtx/quaternion.hpp>

#include "Utils/OxMath.hpp"

namespace ox {
auto DebugDrawList::Geometry::clear(this Geometry& self) -> void {
  ZoneScoped;

  self.lines.clear();
  self.points.clear();
  self.triangles.clear();
}

auto DebugDrawList::reset(this DebugDrawList& self, const bool clear_depth_tested) -> void {
  ZoneScoped;

  self.list.clear();

  if (clear_depth_tested) {
    self.list_depth_tested.clear();
  }
}

auto DebugDrawList::geometry(this DebugDrawList& self, const bool depth_tested) -> Geometry& {
  return depth_tested ? self.list_depth_tested : self.list;
}

auto DebugDrawList::draw_point(
  this DebugDrawList& self,
  const glm::vec3& pos,
  const f32 point_radius,
  const glm::vec4& color,
  const bool depth_tested
) -> void {
  ZoneScoped;

  self.geometry(depth_tested).points.emplace_back(Point{pos, color, point_radius});
}

auto DebugDrawList::draw_line(
  this DebugDrawList& self,
  const glm::vec3& start,
  const glm::vec3& end,
  [[maybe_unused]] const f32 line_width,
  const glm::vec4& color,
  const bool depth_tested
) -> void {
  ZoneScoped;

  self.geometry(depth_tested).lines.emplace_back(Line{start, end, color});
}

auto DebugDrawList::draw_triangle(
  this DebugDrawList& self,
  const glm::vec3& v0,
  const glm::vec3& v1,
  const glm::vec3& v2,
  const glm::vec4& color,
  const bool depth_tested
) -> void {
  ZoneScoped;

  self.geometry(depth_tested).triangles.emplace_back(Triangle{v0, v1, v2, color});
}

auto DebugDrawList::draw_circle(
  this DebugDrawList& self,
  const i32 num_verts,
  const f32 radius,
  const glm::vec3& position,
  const glm::quat& rotation,
  const glm::vec4& color,
  const bool depth_tested
) -> void {
  const f32 step = (2.0f * glm::pi<f32>()) / static_cast<f32>(num_verts); // Use radians

  for (i32 i = 0; i < num_verts; i++) {
    const f32 cx = glm::cos(step * static_cast<f32>(i)) * radius;
    const f32 cy = glm::sin(step * static_cast<f32>(i)) * radius;
    const auto current = glm::vec3(cx, cy, 0.0f);

    const f32 nx = glm::cos(step * static_cast<f32>(i + 1)) * radius;
    const f32 ny = glm::sin(step * static_cast<f32>(i + 1)) * radius;
    const auto next = glm::vec3(nx, ny, 0.0f);

    self.draw_line(position + (rotation * current), position + (rotation * next), 1.0f, color, depth_tested);
  }
}

auto DebugDrawList::draw_arc(
  this DebugDrawList& self,
  const i32 num_verts,
  const f32 radius,
  const glm::vec3& start,
  const glm::vec3& end,
  const glm::quat& rotation,
  const glm::vec4& color,
  const bool depth_tested
) -> void {
  const f32 step = 180.0f / static_cast<f32>(num_verts);
  glm::quat rot = glm::lookAt(rotation * start, rotation * end, glm::vec3(0.0f, 1.0f, 0.0f));
  rot = rotation * rot;

  const glm::vec3 arc_centre = (start + end) * 0.5f;
  for (i32 i = 0; i < num_verts; i++) {
    const f32 cx = glm::cos(step * static_cast<f32>(i)) * radius;
    const f32 cy = glm::sin(step * static_cast<f32>(i)) * radius;
    const auto current = glm::vec3(cx, cy, 0.0f);

    const f32 nx = glm::cos(step * static_cast<f32>(i + 1)) * radius;
    const f32 ny = glm::sin(step * static_cast<f32>(i + 1)) * radius;
    const auto next = glm::vec3(nx, ny, 0.0f);

    self.draw_line(arc_centre + (rot * current), arc_centre + (rot * next), 1.0f, color, depth_tested);
  }
}

auto DebugDrawList::draw_sphere(
  this DebugDrawList& self, const f32 radius, const glm::vec3& position, const glm::vec4& color, const bool depth_tested
) -> void {
  constexpr auto num_verts = 16;

  self.draw_circle(num_verts, radius, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), color, depth_tested);
  self.draw_circle(
    num_verts,
    radius,
    position,
    glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
    color,
    depth_tested
  );
  self.draw_circle(
    num_verts,
    radius,
    position,
    glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
    color,
    depth_tested
  );

  self.draw_circle(
    num_verts,
    radius,
    position,
    glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
    color,
    depth_tested
  );
  self.draw_circle(
    num_verts,
    radius,
    position,
    glm::angleAxis(glm::radians(135.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
    color,
    depth_tested
  );
}

auto DebugDrawList::draw_capsule(
  this DebugDrawList& self,
  const glm::vec3& position,
  const glm::quat& rotation,
  const f32 height,
  const f32 radius,
  const glm::vec4& color,
  const bool depth_tested
) -> void {
  const glm::vec3 up = (rotation * glm::vec3(0.0f, 1.0f, 0.0f));

  const glm::vec3 top_sphere_centre = position + up * (height * 0.5f);
  const glm::vec3 bottom_sphere_centre = position - up * (height * 0.5f);

  self.draw_circle(
    20,
    radius,
    top_sphere_centre,
    rotation * glm::quat(glm::vec3(glm::radians(90.0f), 0.0f, 0.0f)),
    color,
    depth_tested
  );
  self.draw_circle(
    20,
    radius,
    bottom_sphere_centre,
    rotation * glm::quat(glm::vec3(glm::radians(90.0f), 0.0f, 0.0f)),
    color,
    depth_tested
  );

  // Draw 10 arcs
  // Sides
  const f32 step = 360.0f / 20.0f;
  for (i32 i = 0; i < 20; i++) {
    const f32 z = glm::cos(step * static_cast<f32>(i)) * radius;
    const f32 x = glm::sin(step * static_cast<f32>(i)) * radius;

    const glm::vec3 offset = rotation * glm::vec4(x, 0.0f, z, 0.0f);
    self.draw_line(bottom_sphere_centre + offset, top_sphere_centre + offset, 1.0f, color, depth_tested);

    if (i < 10) {
      const f32 z2 = glm::cos(step * static_cast<f32>(i + 10)) * radius;
      const f32 x2 = glm::sin(step * static_cast<f32>(i + 10)) * radius;

      const glm::vec3 offset2 = rotation * glm::vec4(x2, 0.0f, z2, 0.0f);
      // Top Hemishpere
      self.draw_arc(20, radius, top_sphere_centre + offset, top_sphere_centre + offset2, rotation, color, depth_tested);
      // Bottom Hemisphere
      self.draw_arc(
        20,
        radius,
        bottom_sphere_centre + offset,
        bottom_sphere_centre + offset2,
        rotation * glm::quat(glm::vec3(glm::radians(180.0f), 0.0f, 0.0f)),
        color,
        depth_tested
      );
    }
  }
}

auto DebugDrawList::draw_cone(
  this DebugDrawList& self,
  const i32 num_circle_verts,
  const i32 num_lines_to_circle,
  const f32 angle,
  const f32 length,
  const glm::vec3& position,
  const glm::quat& rotation,
  const glm::vec4& color,
  const bool depth_tested
) -> void {
  const f32 end_angle = glm::tan(angle * 0.5f) * length;
  const glm::vec3 forward = -(rotation * glm::vec3(0.0f, 0.0f, -1.0f));
  const glm::vec3 end_position = position + forward * length;
  self.draw_circle(num_circle_verts, end_angle, end_position, rotation, color, depth_tested);

  for (i32 i = 0; i < num_lines_to_circle; i++) {
    const f32 a = static_cast<f32>(i) * 90.0f;
    const glm::vec3 point = rotation * glm::vec3(glm::cos(a), glm::sin(a), 0.0f) * end_angle;
    self.draw_line(position, position + point + forward * length, 0.0f, color, depth_tested);
  }
}

auto DebugDrawList::draw_aabb(
  this DebugDrawList& self,
  const AABB& aabb,
  const glm::vec4& color,
  const bool corners_only,
  const f32 width,
  const bool depth_tested
) -> void {
  const glm::vec3 uuu = aabb.max;
  const glm::vec3 lll = aabb.min;

  const glm::vec3 ull(uuu.x, lll.y, lll.z);
  const glm::vec3 uul(uuu.x, uuu.y, lll.z);
  const glm::vec3 ulu(uuu.x, lll.y, uuu.z);

  const glm::vec3 luu(lll.x, uuu.y, uuu.z);
  const glm::vec3 llu(lll.x, lll.y, uuu.z);
  const glm::vec3 lul(lll.x, uuu.y, lll.z);

  // Draw edges
  if (!corners_only) {
    self.draw_line(luu, uuu, width, color, depth_tested);
    self.draw_line(lul, uul, width, color, depth_tested);
    self.draw_line(llu, ulu, width, color, depth_tested);
    self.draw_line(lll, ull, width, color, depth_tested);

    self.draw_line(lul, lll, width, color, depth_tested);
    self.draw_line(uul, ull, width, color, depth_tested);
    self.draw_line(luu, llu, width, color, depth_tested);
    self.draw_line(uuu, ulu, width, color, depth_tested);

    self.draw_line(lll, llu, width, color, depth_tested);
    self.draw_line(ull, ulu, width, color, depth_tested);
    self.draw_line(lul, luu, width, color, depth_tested);
    self.draw_line(uul, uuu, width, color, depth_tested);
  } else {
    self.draw_line(luu, luu + (uuu - luu) * 0.25f, width, color, depth_tested);
    self.draw_line(luu + (uuu - luu) * 0.75f, uuu, width, color, depth_tested);

    self.draw_line(lul, lul + (uul - lul) * 0.25f, width, color, depth_tested);
    self.draw_line(lul + (uul - lul) * 0.75f, uul, width, color, depth_tested);

    self.draw_line(llu, llu + (ulu - llu) * 0.25f, width, color, depth_tested);
    self.draw_line(llu + (ulu - llu) * 0.75f, ulu, width, color, depth_tested);

    self.draw_line(lll, lll + (ull - lll) * 0.25f, width, color, depth_tested);
    self.draw_line(lll + (ull - lll) * 0.75f, ull, width, color, depth_tested);

    self.draw_line(lul, lul + (lll - lul) * 0.25f, width, color, depth_tested);
    self.draw_line(lul + (lll - lul) * 0.75f, lll, width, color, depth_tested);

    self.draw_line(uul, uul + (ull - uul) * 0.25f, width, color, depth_tested);
    self.draw_line(uul + (ull - uul) * 0.75f, ull, width, color, depth_tested);

    self.draw_line(luu, luu + (llu - luu) * 0.25f, width, color, depth_tested);
    self.draw_line(luu + (llu - luu) * 0.75f, llu, width, color, depth_tested);

    self.draw_line(uuu, uuu + (ulu - uuu) * 0.25f, width, color, depth_tested);
    self.draw_line(uuu + (ulu - uuu) * 0.75f, ulu, width, color, depth_tested);

    self.draw_line(lll, lll + (llu - lll) * 0.25f, width, color, depth_tested);
    self.draw_line(lll + (llu - lll) * 0.75f, llu, width, color, depth_tested);

    self.draw_line(ull, ull + (ulu - ull) * 0.25f, width, color, depth_tested);
    self.draw_line(ull + (ulu - ull) * 0.75f, ulu, width, color, depth_tested);

    self.draw_line(lul, lul + (luu - lul) * 0.25f, width, color, depth_tested);
    self.draw_line(lul + (luu - lul) * 0.75f, luu, width, color, depth_tested);

    self.draw_line(uul, uul + (uuu - uul) * 0.25f, width, color, depth_tested);
    self.draw_line(uul + (uuu - uul) * 0.75f, uuu, width, color, depth_tested);
  }
}

auto DebugDrawList::draw_frustum(
  this DebugDrawList& self,
  const glm::mat4& frustum,
  const glm::vec4& color,
  [[maybe_unused]] const f32 near,
  [[maybe_unused]] const f32 far
) -> void {
  // Get the inverse view-projection matrix
  const glm::mat4 inv_frustum = glm::inverse(frustum);

  // For reversed-Z: near plane is at z = 1, far plane is at z = 0 in clip space
  const std::array clip_corners = {
    // Near plane corners (z = 1 for reversed-Z)
    glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f), // bottom-left-near
    glm::vec4(1.0f, -1.0f, 1.0f, 1.0f),  // bottom-right-near
    glm::vec4(-1.0f, 1.0f, 1.0f, 1.0f),  // top-left-near
    glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),   // top-right-near

    // Far plane corners (z = 0 for reversed-Z)
    glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f), // bottom-left-far
    glm::vec4(1.0f, -1.0f, 0.0f, 1.0f),  // bottom-right-far
    glm::vec4(-1.0f, 1.0f, 0.0f, 1.0f),  // top-left-far
    glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)    // top-right-far
  };

  // Transform corners to world space and apply perspective division
  std::array<glm::vec3, clip_corners.size()> world_corners = {};
  for (usize i = 0; i < clip_corners.size(); i++) {
    glm::vec4 world_pos = inv_frustum * clip_corners[i];
    world_pos /= world_pos.w; // Perspective division
    world_corners[i] = glm::vec3(world_pos);
  }

  // Extract individual corners for readability
  const glm::vec3 bln = world_corners[0]; // bottom-left-near
  const glm::vec3 brn = world_corners[1]; // bottom-right-near
  const glm::vec3 tln = world_corners[2]; // top-left-near
  const glm::vec3 trn = world_corners[3]; // top-right-near
  const glm::vec3 blf = world_corners[4]; // bottom-left-far
  const glm::vec3 brf = world_corners[5]; // bottom-right-far
  const glm::vec3 tlf = world_corners[6]; // top-left-far
  const glm::vec3 trf = world_corners[7]; // top-right-far

  // Connect-the-dots
  // Near and far "squares"
  self.draw_line(tln, trn, 1.0f, color, false);
  self.draw_line(bln, brn, 1.0f, color, false);
  self.draw_line(tln, bln, 1.0f, color, false);
  self.draw_line(trn, brn, 1.0f, color, false);
  self.draw_line(tlf, trf, 1.0f, color, false);
  self.draw_line(blf, brf, 1.0f, color, false);
  self.draw_line(tlf, blf, 1.0f, color, false);
  self.draw_line(trf, brf, 1.0f, color, false);

  // Lines connecting near and far planes
  self.draw_line(tln, tlf, 1.0f, color, false);
  self.draw_line(trn, trf, 1.0f, color, false);
  self.draw_line(bln, blf, 1.0f, color, false);
  self.draw_line(brn, brf, 1.0f, color, false);
}

auto DebugDrawList::draw_ray(
  this DebugDrawList& self, const RayCast& ray, const glm::vec4& color, const f32 distance, const bool depth_tested
) -> void {
  self.draw_line(ray.get_origin(), ray.get_origin() + ray.get_direction() * distance, 1.0f, color, depth_tested);
}

auto DebugDrawList::get_lines(this const DebugDrawList& self, const bool depth_tested) -> std::span<const Line> {
  return !depth_tested ? self.list.lines : self.list_depth_tested.lines;
}

auto DebugDrawList::get_points(this const DebugDrawList& self, const bool depth_tested) -> std::span<const Point> {
  return !depth_tested ? self.list.points : self.list_depth_tested.points;
}

auto DebugDrawList::get_triangles(this const DebugDrawList& self, const bool depth_tested)
  -> std::span<const Triangle> {
  return !depth_tested ? self.list.triangles : self.list_depth_tested.triangles;
}

auto DebugDrawList::get_vertices_from_lines(const std::span<const Line> lines) -> std::pair<std::vector<Vertex>, u32> {
  ZoneScoped;

  std::vector<Vertex> vertices = {};
  vertices.reserve(lines.size() * 2);
  auto indices = 0_u32;

  for (const auto& line : lines) {
    vertices.emplace_back(Vertex{.position = line.p1, .color = glm::packSnorm2x16(math::float32x3_to_oct(line.col))});
    vertices.emplace_back(Vertex{.position = line.p2, .color = glm::packSnorm2x16(math::float32x3_to_oct(line.col))});

    indices += 2;
  }

  return {vertices, indices};
}

auto DebugDrawList::get_vertices_from_triangles(const std::span<const Triangle> triangles)
  -> std::pair<std::vector<Vertex>, u32> {
  ZoneScoped;

  std::vector<Vertex> vertices = {};
  vertices.reserve(triangles.size() * 3);
  auto indices = 0_u32;

  for (const auto& tri : triangles) {
    vertices.emplace_back(Vertex{.position = tri.p1, .color = glm::packSnorm2x16(math::float32x3_to_oct(tri.col))});
    vertices.emplace_back(Vertex{.position = tri.p2, .color = glm::packSnorm2x16(math::float32x3_to_oct(tri.col))});
    vertices.emplace_back(Vertex{.position = tri.p3, .color = glm::packSnorm2x16(math::float32x3_to_oct(tri.col))});

    indices += 3;
  }

  return {vertices, indices};
}
} // namespace ox
