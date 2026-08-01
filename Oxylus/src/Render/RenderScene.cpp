#include "Render/RenderScene.hpp"

#include "Asset/AssetManager.hpp"
#include "Core/App.hpp"
#include "Memory/SlotMap.hpp"

namespace ox {
RenderScene::~RenderScene() = default;

auto RenderScene::init(this RenderScene& self) -> void {
  ZoneScoped;

  if (!App::has_mod<Renderer>()) {
    return;
  }

  self.instance = App::mod<Renderer>().new_instance();
}

auto RenderScene::prepare(this RenderScene& self, const FrameSnapshot& snapshot, const RendererCVar& cvar) -> void {
  ZoneScoped;

  if (!self.instance) {
    return;
  }

  auto& asset_man = App::mod<AssetManager>();

  if (snapshot.mesh_instances_rebuilt) {
    self.gpu_meshes.clear();
    self.gpu_mesh_instances.clear();
    self.mesh_slot_to_gpu_index.clear();
    self.transform_index_to_entity.clear();

    auto unique_mesh_to_gpu_mesh = ankerl::unordered_dense::map<std::pair<UUID, u32>, u32>();
    auto meshlet_instance_visibility_offset = 0_u32;
    auto max_meshlet_instance_count = 0_u32;

    for (const auto& mesh_instance : snapshot.mesh_instances) {
      const auto model = asset_man.get_model(mesh_instance.model_uuid);
      if (!model) {
        continue;
      }

      const auto material_asset = asset_man.get_asset(mesh_instance.material_uuid);
      const auto material_id = material_asset ? material_asset->material_id
                                              : asset_man.get_null_material()->material_id;

      const auto unique_mesh = std::pair(mesh_instance.model_uuid, mesh_instance.mesh_node_index);
      auto mesh_index = 0_u32;
      if (const auto it = unique_mesh_to_gpu_mesh.find(unique_mesh); it != unique_mesh_to_gpu_mesh.end()) {
        mesh_index = it->second;
      } else {
        mesh_index = static_cast<u32>(self.gpu_meshes.size());
        self.gpu_meshes.emplace_back(model->gpu_meshes[mesh_instance.mesh_node_index]);
        unique_mesh_to_gpu_mesh.emplace(unique_mesh, mesh_index);
      }

      const auto lod0_meshlet_count = model->lod0_meshlet_counts[mesh_instance.mesh_node_index];

      auto& gpu_mesh_instance = self.gpu_mesh_instances.emplace_back();
      gpu_mesh_instance.mesh_index = mesh_index;
      gpu_mesh_instance.lod_index = 0;
      gpu_mesh_instance.material_index = SlotMap_decode_id(material_id).index;
      gpu_mesh_instance.transform_index = mesh_instance.transform_index;
      gpu_mesh_instance.meshlet_instance_visibility_offset = meshlet_instance_visibility_offset;

      self.mesh_slot_to_gpu_index[mesh_instance.slot_index] = static_cast<u32>(self.gpu_mesh_instances.size() - 1);
      self.transform_index_to_entity[mesh_instance.transform_index] = mesh_instance.entity;

      meshlet_instance_visibility_offset += lod0_meshlet_count;
      max_meshlet_instance_count += lod0_meshlet_count;
    }

    self.mesh_instance_count = static_cast<u32>(self.gpu_mesh_instances.size());
    self.max_meshlet_instance_count = max_meshlet_instance_count;
  } else if (!snapshot.dirty_mesh_instance_slots.empty()) {
    // The instance list did not change shape, so the GPU arrays still stand; only the slot to
    // array-index mapping has to be re-derived to translate the dirty list.
    self.mesh_slot_to_gpu_index.clear();
    auto gpu_index = 0_u32;
    for (const auto& mesh_instance : snapshot.mesh_instances) {
      self.mesh_slot_to_gpu_index[mesh_instance.slot_index] = gpu_index++;
    }
  }

  self.dirty_mesh_instance_indices.clear();
  self.dirty_mesh_instance_indices.reserve(snapshot.dirty_mesh_instance_slots.size());
  for (const auto slot_index : snapshot.dirty_mesh_instance_slots) {
    if (const auto it = self.mesh_slot_to_gpu_index.find(slot_index); it != self.mesh_slot_to_gpu_index.end()) {
      self.dirty_mesh_instance_indices.push_back(it->second);
    }
  }

  auto update_info = RendererInstanceUpdateInfo{
    .mesh_instance_count = self.mesh_instance_count,
    .max_meshlet_instance_count = self.max_meshlet_instance_count,
    .dirty_transform_ids = snapshot.dirty_transform_ids,
    .gpu_transforms = snapshot.transforms,
    .gpu_meshes = self.gpu_meshes,
    .gpu_mesh_instances = self.gpu_mesh_instances,
    .dirty_mesh_instance_indices = self.dirty_mesh_instance_indices,
  };

  self.instance->prepare(update_info, snapshot, cvar);
}

auto RenderScene::render(
  this RenderScene& self,
  vuk::Value<vuk::ImageAttachment>&& dst_attachment,
  const Renderer::RenderInfo& render_info,
  const RendererCVar& cvar
) -> vuk::Value<vuk::ImageAttachment> {
  ZoneScoped;

  OX_CHECK_NULL(self.instance);

  return self.instance->render(std::move(dst_attachment), render_info, cvar);
}

auto RenderScene::entity_for_transform_index(this const RenderScene& self, const u32 transform_index) -> EntityHandle {
  const auto it = self.transform_index_to_entity.find(transform_index);
  return it == self.transform_index_to_entity.end() ? EntityHandle::Invalid : it->second;
}
} // namespace ox
