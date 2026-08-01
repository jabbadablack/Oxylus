#pragma once

#include <ankerl/unordered_dense.h>
#include <memory>
#include <vector>

#include "Render/Renderer.hpp"
#include "Render/RendererCVar.hpp"
#include "Render/RendererInstance.hpp"
#include "Server/FrameSnapshot.hpp"

namespace ox {
// The presentation-side counterpart to a Scene: it owns the renderer instance for one view and
// turns a FrameSnapshot into the GPU arrays that instance needs.
//
// This is what used to live inside Scene as a unique_ptr<RendererInstance> plus a block of mesh
// resolution in the middle of runtime_update. Moving it here is what lets Scene.hpp stop including
// vuk, which in turn lets the scene, physics and scripting sources build headless.
class RenderScene {
public:
  RenderScene() = default;
  ~RenderScene();

  RenderScene(const RenderScene&) = delete;
  RenderScene& operator=(const RenderScene&) = delete;

  auto init(this RenderScene& self) -> void;

  // Resolves the snapshot's logical mesh instances (UUIDs, slot indices) into GPU meshes and
  // instances, then hands everything to the renderer instance. Must run before render().
  auto prepare(this RenderScene& self, const FrameSnapshot& snapshot, const RendererCVar& cvar) -> void;

  auto render(
    this RenderScene& self,
    vuk::Value<vuk::ImageAttachment>&& dst_attachment,
    const Renderer::RenderInfo& render_info,
    const RendererCVar& cvar
  ) -> vuk::Value<vuk::ImageAttachment>;

  auto get_instance(this const RenderScene& self) -> RendererInstance* { return self.instance.get(); }

  // Which entity owns the mesh instance drawn at a given transform index. Rebuilt every prepare
  // from the snapshot, and used by viewport mouse picking.
  auto entity_for_transform_index(this const RenderScene& self, u32 transform_index) -> EntityHandle;

private:
  std::unique_ptr<RendererInstance> instance = nullptr;

  // Persistent so a steady-state frame reuses the capacity instead of reallocating.
  std::vector<GPU::Mesh> gpu_meshes = {};
  std::vector<GPU::MeshInstance> gpu_mesh_instances = {};
  std::vector<u32> dirty_mesh_instance_indices = {};
  ankerl::unordered_dense::map<u32, u32> mesh_slot_to_gpu_index = {};
  ankerl::unordered_dense::map<u32, EntityHandle> transform_index_to_entity = {};

  u32 mesh_instance_count = 0;
  u32 max_meshlet_instance_count = 0;
};
} // namespace ox
