#include <gtest/gtest.h>

#include "Scene/DebugDrawList.hpp"

// DebugDrawList is simulation-side geometry: the scene produces it without knowing a GPU exists.
// This binary links ox::OxylusServerLib only, so it also proves the type stayed free of vuk.

namespace ox {
TEST(DebugDrawList, DrawLineGoesToTheRequestedDepthList) {
  auto draw_list = DebugDrawList{};

  draw_list.draw_line({0, 0, 0}, {1, 0, 0}, 1.f, glm::vec4(1.f), false);
  draw_list.draw_line({0, 0, 0}, {0, 1, 0}, 1.f, glm::vec4(1.f), true);

  EXPECT_EQ(draw_list.get_lines(false).size(), 1_sz);
  EXPECT_EQ(draw_list.get_lines(true).size(), 1_sz);
}

TEST(DebugDrawList, ResetClearsTrianglesAsWellAsLinesAndPoints) {
  auto draw_list = DebugDrawList{};

  draw_list.draw_line({0, 0, 0}, {1, 0, 0}, 1.f, glm::vec4(1.f), false);
  draw_list.draw_point({0, 0, 0}, 1.f, glm::vec4(1.f), false);
  draw_list.draw_triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, glm::vec4(1.f), false);

  ASSERT_EQ(draw_list.get_triangles(false).size(), 1_sz);

  draw_list.reset();

  EXPECT_TRUE(draw_list.get_lines(false).empty());
  EXPECT_TRUE(draw_list.get_points(false).empty());
  // Triangles used to survive reset, so physics debug geometry accumulated every frame forever.
  EXPECT_TRUE(draw_list.get_triangles(false).empty());
}

TEST(DebugDrawList, ResetCanKeepDepthTestedGeometry) {
  auto draw_list = DebugDrawList{};

  draw_list.draw_triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, glm::vec4(1.f), true);
  draw_list.draw_triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, glm::vec4(1.f), false);

  draw_list.reset(false);

  EXPECT_TRUE(draw_list.get_triangles(false).empty());
  EXPECT_EQ(draw_list.get_triangles(true).size(), 1_sz);
}

TEST(DebugDrawList, AabbDrawsTwelveEdges) {
  auto draw_list = DebugDrawList{};

  const auto aabb = AABB{glm::vec3(-1.f), glm::vec3(1.f)};
  draw_list.draw_aabb(aabb, glm::vec4(1.f), false, 1.f, false);

  EXPECT_EQ(draw_list.get_lines(false).size(), 12_sz);
}

TEST(DebugDrawList, CapsuleArcsRespectTheRequestedDepthList) {
  auto draw_list = DebugDrawList{};

  draw_list.draw_capsule({0, 0, 0}, glm::quat(1.f, 0.f, 0.f, 0.f), 2.f, 0.5f, glm::vec4(1.f), true);

  // Arcs used to be emitted through a global module rather than the list being drawn into, so a
  // depth-tested capsule leaked most of its geometry into the non-depth-tested list.
  EXPECT_TRUE(draw_list.get_lines(false).empty());
  EXPECT_FALSE(draw_list.get_lines(true).empty());
}

TEST(DebugDrawList, LineAndTriangleVertexConversionCounts) {
  auto draw_list = DebugDrawList{};

  draw_list.draw_line({0, 0, 0}, {1, 0, 0}, 1.f, glm::vec4(1.f), false);
  draw_list.draw_line({0, 0, 0}, {0, 1, 0}, 1.f, glm::vec4(1.f), false);
  draw_list.draw_triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, glm::vec4(1.f), false);

  const auto [line_vertices, line_indices] = DebugDrawList::get_vertices_from_lines(draw_list.get_lines(false));
  EXPECT_EQ(line_vertices.size(), 4_sz);
  EXPECT_EQ(line_indices, 4_u32);

  const auto [tri_vertices, tri_indices] = DebugDrawList::get_vertices_from_triangles(draw_list.get_triangles(false));
  EXPECT_EQ(tri_vertices.size(), 3_sz);
  EXPECT_EQ(tri_indices, 3_u32);
}
} // namespace ox
