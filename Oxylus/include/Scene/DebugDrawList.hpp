#pragma once

#include <glm/ext/quaternion_float.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <utility>
#include <vector>

#include "Core/Types.hpp"
#include "Physics/RayCast.hpp"
#include "Render/BoundingVolume.hpp"

namespace ox {
// CPU-side immediate-mode debug geometry, accumulated over a frame and consumed by the renderer,
// which turns it into vertices. Deliberately free of vuk: the simulation produces debug draws
// (bounding boxes, physics shapes) without knowing a GPU exists, and the client produces its own
// (editor gizmos). Each owner holds its own list.
struct DebugDrawList {
  struct Vertex {
    glm::vec3 position = {};
    u32 color = 0;
  };

  struct Line {
    glm::vec3 p1 = {};
    glm::vec3 p2 = {};
    glm::vec4 col = {};
  };

  struct Point {
    glm::vec3 p1 = {};
    glm::vec4 col = {};
    f32 size = 0.f;
  };

  struct Triangle {
    glm::vec3 p1 = {};
    glm::vec3 p2 = {};
    glm::vec3 p3 = {};
    glm::vec4 col = {};
  };

  struct Geometry {
    std::vector<Line> lines = {};
    std::vector<Point> points = {};
    std::vector<Triangle> triangles = {};

    auto clear(this Geometry& self) -> void;
  };

  Geometry list = {};
  Geometry list_depth_tested = {};

  auto reset(this DebugDrawList& self, bool clear_depth_tested = true) -> void;

  auto geometry(this DebugDrawList& self, bool depth_tested) -> Geometry&;

  /// Draw Point (circle)
  auto draw_point(
    this DebugDrawList& self,
    const glm::vec3& pos,
    f32 point_radius,
    const glm::vec4& color = glm::vec4(1.f),
    bool depth_tested = false
  ) -> void;

  /// Draw Line with a given thickness
  auto draw_line(
    this DebugDrawList& self,
    const glm::vec3& start,
    const glm::vec3& end,
    f32 line_width,
    const glm::vec4& color = glm::vec4(1.f),
    bool depth_tested = false
  ) -> void;

  auto draw_triangle(
    this DebugDrawList& self,
    const glm::vec3& v0,
    const glm::vec3& v1,
    const glm::vec3& v2,
    const glm::vec4& color,
    bool depth_tested = false
  ) -> void;

  auto draw_circle(
    this DebugDrawList& self,
    i32 num_verts,
    f32 radius,
    const glm::vec3& position,
    const glm::quat& rotation,
    const glm::vec4& color,
    bool depth_tested = false
  ) -> void;

  auto draw_arc(
    this DebugDrawList& self,
    i32 num_verts,
    f32 radius,
    const glm::vec3& start,
    const glm::vec3& end,
    const glm::quat& rotation,
    const glm::vec4& color,
    bool depth_tested = false
  ) -> void;

  auto draw_sphere(
    this DebugDrawList& self, f32 radius, const glm::vec3& position, const glm::vec4& color, bool depth_tested = false
  ) -> void;

  auto draw_capsule(
    this DebugDrawList& self,
    const glm::vec3& position,
    const glm::quat& rotation,
    f32 height,
    f32 radius,
    const glm::vec4& color,
    bool depth_tested = false
  ) -> void;

  auto draw_cone(
    this DebugDrawList& self,
    i32 num_circle_verts,
    i32 num_lines_to_circle,
    f32 angle,
    f32 length,
    const glm::vec3& position,
    const glm::quat& rotation,
    const glm::vec4& color,
    bool depth_tested = false
  ) -> void;

  auto draw_aabb(
    this DebugDrawList& self,
    const AABB& aabb,
    const glm::vec4& color = glm::vec4(1.f),
    bool corners_only = false,
    f32 width = 1.f,
    bool depth_tested = false
  ) -> void;

  auto draw_frustum(this DebugDrawList& self, const glm::mat4& frustum, const glm::vec4& color, f32 near, f32 far)
    -> void;

  auto draw_ray(
    this DebugDrawList& self, const RayCast& ray, const glm::vec4& color, f32 distance, bool depth_tested = false
  ) -> void;

  auto get_lines(this const DebugDrawList& self, bool depth_tested = true) -> std::span<const Line>;
  auto get_points(this const DebugDrawList& self, bool depth_tested = true) -> std::span<const Point>;
  auto get_triangles(this const DebugDrawList& self, bool depth_tested = true) -> std::span<const Triangle>;

  static auto get_vertices_from_lines(std::span<const Line> lines) -> std::pair<std::vector<Vertex>, u32>;
  static auto get_vertices_from_triangles(std::span<const Triangle> triangles) -> std::pair<std::vector<Vertex>, u32>;
};
} // namespace ox
