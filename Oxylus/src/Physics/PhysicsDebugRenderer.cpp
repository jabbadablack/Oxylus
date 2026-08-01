#include "Physics/PhysicsDebugRenderer.hpp"

#include "Utils/OxMath.hpp"

namespace ox {
PhysicsDebugRenderer::PhysicsDebugRenderer(DebugDrawList& target_draw_list) : draw_list(target_draw_list) {
  DebugRenderer::Initialize();
}

void PhysicsDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) {
  draw_list.draw_line(
    math::from_jolt(inFrom),
    math::from_jolt(inTo),
    1.0f,
    math::from_jolt(inColor.ToVec4()),
    draw_depth_tested
  );
}

void PhysicsDebugRenderer::DrawTriangle(
  JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow
) {
  draw_list.draw_triangle(
    math::from_jolt(inV1),
    math::from_jolt(inV2),
    math::from_jolt(inV3),
    math::from_jolt(inColor.ToVec4()),
    draw_depth_tested
  );
}

JPH::DebugRenderer::Batch PhysicsDebugRenderer::CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) {
  TriangleBatch* pBatch = new TriangleBatch;
  pBatch->triangles.reserve(inTriangleCount);

  for (int i = 0; i < inTriangleCount; ++i) {
    auto& t = pBatch->triangles.emplace_back();
    t.p1 = math::from_jolt(JPH::Vec3{inTriangles[i].mV[0].mPosition});
    t.p2 = math::from_jolt(JPH::Vec3{inTriangles[i].mV[1].mPosition});
    t.p3 = math::from_jolt(JPH::Vec3{inTriangles[i].mV[2].mPosition});
    t.col = math::from_jolt(inTriangles[i].mV[0].mColor.ToVec4());
  }

  return pBatch;
}

JPH::DebugRenderer::Batch PhysicsDebugRenderer::CreateTriangleBatch(
  const Vertex* inVertices, int inVertexCount, const u32* inIndices, int inIndexCount
) {
  const u32 numTris = inIndexCount / 3;

  TriangleBatch* pBatch = new TriangleBatch;
  pBatch->triangles.reserve(numTris);

  u32 index = 0;

  for (u32 i = 0; i < numTris; ++i) {
    auto& t = pBatch->triangles.emplace_back();
    t.p1 = math::from_jolt(JPH::Vec3{inVertices[inIndices[index + 0]].mPosition});
    t.p2 = math::from_jolt(JPH::Vec3{inVertices[inIndices[index + 1]].mPosition});
    t.p3 = math::from_jolt(JPH::Vec3{inVertices[inIndices[index + 2]].mPosition});
    t.col = math::from_jolt(inVertices[inIndices[index + 0]].mColor.ToVec4());

    index += 3;
  }

  return pBatch;
}

void PhysicsDebugRenderer::DrawGeometry(
  JPH::RMat44Arg inModelMatrix,
  const JPH::AABox& inWorldSpaceBounds,
  float inLODScaleSq,
  JPH::ColorArg inModelColor,
  const GeometryRef& geometry,
  ECullMode inCullMode,
  ECastShadow inCastShadow,
  EDrawMode inDrawMode
) {
  if (geometry == nullptr)
    return;

  u32 uiLod = 0;
  if (geometry->mLODs.size() > 1)
    uiLod = 1;
  if (geometry->mLODs.size() > 2)
    uiLod = 2;

  const TriangleBatch* pBatch = static_cast<const TriangleBatch*>(geometry->mLODs[uiLod].mTriangleBatch.GetPtr());

  const glm::mat4 trans = reinterpret_cast<const glm::mat4&>(inModelMatrix);
  const glm::vec4 color = math::from_jolt(inModelColor.ToVec4());

  auto& geo = draw_list.geometry(draw_depth_tested);

  if (inDrawMode == JPH::DebugRenderer::EDrawMode::Solid) {
    if (inCullMode == JPH::DebugRenderer::ECullMode::CullBackFace || inCullMode == JPH::DebugRenderer::ECullMode::Off) {
      for (u32 t = 0; t < pBatch->triangles.size(); ++t) {
        auto& tri = geo.triangles.emplace_back();
        tri.col = pBatch->triangles[t].col * color;
        tri.p1 = trans * glm::vec4(pBatch->triangles[t].p1, 1.f);
        tri.p2 = trans * glm::vec4(pBatch->triangles[t].p2, 1.f);
        tri.p3 = trans * glm::vec4(pBatch->triangles[t].p3, 1.f);
      }
    }

    if (
      inCullMode == JPH::DebugRenderer::ECullMode::CullFrontFace || inCullMode == JPH::DebugRenderer::ECullMode::Off
    ) {
      for (u32 t = 0; t < pBatch->triangles.size(); ++t) {
        auto& tri = geo.triangles.emplace_back();
        tri.col = pBatch->triangles[t].col * color;
        tri.p1 = trans * glm::vec4(pBatch->triangles[t].p1, 1.f);
        tri.p2 = trans * glm::vec4(pBatch->triangles[t].p3, 1.f);
        tri.p3 = trans * glm::vec4(pBatch->triangles[t].p2, 1.f);
      }
    }
  } else {
    for (u32 t = 0; t < pBatch->triangles.size(); ++t) {
      const auto& tri = pBatch->triangles[t];
      const auto col = pBatch->triangles[t].col * color;

      const glm::vec3 v0 = trans * glm::vec4(tri.p1, 1.0f);
      const glm::vec3 v1 = trans * glm::vec4(tri.p2, 1.0f);
      const glm::vec3 v2 = trans * glm::vec4(tri.p3, 1.0f);

      geo.lines.emplace_back(DebugDrawList::Line{v0, v1, col});
      geo.lines.emplace_back(DebugDrawList::Line{v1, v2, col});
      geo.lines.emplace_back(DebugDrawList::Line{v2, v0, col});
    }
  }
}

void PhysicsDebugRenderer::DrawText3D(
  JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight
) {}
} // namespace ox
