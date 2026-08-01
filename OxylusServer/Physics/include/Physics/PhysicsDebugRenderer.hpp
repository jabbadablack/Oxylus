#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRenderer.h>

#include "Core/Types.hpp"
#include "Scene/DebugDrawList.hpp"

namespace ox {
// Jolt's debug draw sink. Writes into whichever DebugDrawList it was constructed against - the
// owning Scene's - rather than reaching for a global renderer module, so physics debug output is
// simulation-side data like every other debug draw.
class PhysicsDebugRenderer final : public JPH::DebugRenderer {
public:
  bool draw_depth_tested = false; // TODO: configurable via cvar

  struct TriangleBatch : public JPH::RefTargetVirtual {
    std::vector<DebugDrawList::Triangle> triangles;

    int ref_count = 0;

    virtual void AddRef() override { ++ref_count; }

    virtual void Release() override {
      --ref_count;

      if (ref_count == 0) {
        auto* pThis = this;
        delete pThis;
      }
    }
  };

  explicit PhysicsDebugRenderer(DebugDrawList& target_draw_list);

  virtual void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
  virtual void DrawTriangle(
    JPH::RVec3Arg inV1,
    JPH::RVec3Arg inV2,
    JPH::RVec3Arg inV3,
    JPH::ColorArg inColor,
    ECastShadow inCastShadow = ECastShadow::Off
  ) override;
  virtual Batch CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) override;
  virtual Batch CreateTriangleBatch(
    const Vertex* inVertices, int inVertexCount, const u32* inIndices, int inIndexCount
  ) override;
  virtual void DrawGeometry(
    JPH::RMat44Arg inModelMatrix,
    const JPH::AABox& inWorldSpaceBounds,
    float inLODScaleSq,
    JPH::ColorArg inModelColor,
    const GeometryRef& inGeometry,
    ECullMode inCullMode,
    ECastShadow inCastShadow,
    EDrawMode inDrawMode
  ) override;
  virtual void DrawText3D(
    JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight
  ) override;

private:
  DebugDrawList& draw_list;
};
} // namespace ox
