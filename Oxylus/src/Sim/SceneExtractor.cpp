#include "Sim/SceneExtractor.hpp"

#include "Core/Enum.hpp"
#include "Memory/SlotMap.hpp"
#include "Scene/Scene.hpp"
#include "Utils/Log.hpp"
#include "Utils/OxMath.hpp"

namespace ox {
auto camera_data_from_component(const CameraComponent& camera) -> GPU::CameraData {
  auto data = GPU::CameraData{
    .position = glm::vec4(camera.position, 0.0f),
    .projection = camera.get_projection_matrix(),
    .inv_projection = camera.get_inv_projection_matrix(),
    .view = camera.get_view_matrix(),
    .inv_view = camera.get_inv_view_matrix(),
    .projection_view = camera.get_projection_matrix() * camera.get_view_matrix(),
    .inv_projection_view = camera.get_inverse_projection_view(),
    .temporalaa_jitter = camera.jitter,
    .near_clip = camera.near_clip,
    .far_clip = camera.far_clip,
    .fov = camera.fov,
    .output_index = 0,
    .acceptable_lod_error = 2.0f,
  };

  math::calc_frustum_planes(data.projection_view, data.frustum_planes);

  return data;
}

auto SceneExtractor::init(this SceneExtractor& self, flecs::world& world) -> void {
  ZoneScoped;

  self.camera_query = world.query_builder<const TransformComponent, const CameraComponent>().build();
  self.light_query = world.query_builder<const TransformComponent, const LightComponent>().build();
  self.sprite_query = world.query_builder<const TransformComponent, const SpriteComponent>().build();
  self.particle_query = world.query_builder<const TransformComponent, const ParticleComponent>().build();
  self.auto_exposure_query = world.query_builder<const AutoExposureComponent>().build();
  self.vignette_query = world.query_builder<const VignetteComponent>().build();
  self.chromatic_aberration_query = world.query_builder<const ChromaticAberrationComponent>().build();
  self.film_grain_query = world.query_builder<const FilmGrainComponent>().build();
  self.tonemap_query = world.query_builder<const TonemappingComponent>().build();

  self.initialized = true;
}

auto SceneExtractor::deinit(this SceneExtractor& self) -> void {
  ZoneScoped;

  if (!self.initialized) {
    return;
  }

  self.camera_query.destruct();
  self.light_query.destruct();
  self.sprite_query.destruct();
  self.particle_query.destruct();
  self.auto_exposure_query.destruct();
  self.vignette_query.destruct();
  self.chromatic_aberration_query.destruct();
  self.film_grain_query.destruct();
  self.tonemap_query.destruct();

  self.initialized = false;
}

auto SceneExtractor::extract(
  this SceneExtractor& self, Scene& scene, const std::span<const ViewRequest> views, FrameSnapshot& out
) -> void {
  ZoneScoped;

  if (!self.initialized) {
    return;
  }

  out.clear();

  self.extract_views(scene, views, out);
  self.extract_lights_and_sky(scene, out);
  self.extract_post_process(out);
  self.extract_sprites(scene, out);
  self.extract_geometry(scene, out);
}

auto SceneExtractor::extract_geometry(this SceneExtractor& self, Scene& scene, FrameSnapshot& out) -> void {
  ZoneScoped;

  const auto transform_slots = scene.transforms.slots_unsafe();
  out.transforms.assign(transform_slots.begin(), transform_slots.end());
  out.dirty_transform_ids.assign(scene.dirty_transforms.begin(), scene.dirty_transforms.end());

  out.mesh_instances_rebuilt = scene.meshes_dirty;

  // Instances go over as UUIDs and slot indices. Turning those into device addresses and bindless
  // material indices is the client's job, and the only side that can do it.
  self.slot_to_entity.clear();
  self.slot_to_entity.reserve(scene.entity_to_mesh_instance_map.size());
  for (const auto& [entity, mesh_instance_id] : scene.entity_to_mesh_instance_map) {
    self.slot_to_entity.emplace(SlotMap_decode_id(mesh_instance_id).index, static_cast<EntityHandle>(entity.id()));
  }

  scene.mesh_instances.for_each_active([&out, &self](usize index, const MeshInstance& mesh_instance) {
    const auto it = self.slot_to_entity.find(static_cast<u32>(index));
    const auto entity = it == self.slot_to_entity.end() ? EntityHandle::Invalid : it->second;

    out.mesh_instances.emplace_back(
      MeshInstanceSnapshot{
        .model_uuid = mesh_instance.model_uuid,
        .mesh_node_index = static_cast<u32>(mesh_instance.mesh_node_index),
        .material_uuid = mesh_instance.material_uuid,
        .transform_index = SlotMap_decode_id(mesh_instance.transform_id).index,
        .slot_index = static_cast<u32>(index),
        .entity = entity,
      }
    );
  });

  out.dirty_mesh_instance_slots.reserve(scene.dirty_mesh_instances.size());
  for (const auto mesh_instance_id : scene.dirty_mesh_instances) {
    out.dirty_mesh_instance_slots.push_back(SlotMap_decode_id(mesh_instance_id).index);
  }

  // Debug geometry is immediate-mode: drain it into the snapshot and clear, so it never survives a
  // frame whether or not anything is drawing.
  const auto lines = scene.debug_draw_list.get_lines(false);
  const auto triangles = scene.debug_draw_list.get_triangles(false);
  out.debug_lines.assign(lines.begin(), lines.end());
  out.debug_triangles.assign(triangles.begin(), triangles.end());
  scene.debug_draw_list.reset();
}

auto SceneExtractor::extract_views(
  this SceneExtractor& self, Scene& scene, const std::span<const ViewRequest> views, FrameSnapshot& out
) -> void {
  ZoneScoped;

  // Whichever camera the query visits last. This is what the renderer's old inline query settled on
  // (it assigned to `current_camera` on every hit), and a SimEntity view that names no usable
  // entity - play mode, before anything has bound a gameplay camera - falls back to it.
  auto fallback_camera = option<CameraComponent>(nullopt);
  auto fallback_entity = EntityHandle::Invalid;
  self.camera_query.each([&fallback_camera,
                          &fallback_entity](flecs::entity e, const TransformComponent&, const CameraComponent& camera) {
    fallback_camera = camera;
    fallback_entity = static_cast<EntityHandle>(e.id());
  });

  const auto resolve = [&](const EntityHandle handle) -> option<CameraComponent> {
    const auto entity = scene.world.entity(static_cast<flecs::entity_t>(handle));
    if (handle != EntityHandle::Invalid && entity.is_valid()) {
      if (const auto* camera = entity.try_get<CameraComponent>()) {
        return *camera;
      }
    }

    return fallback_camera;
  };

  // A client-supplied camera is copied through untouched; the simulation has no opinion about where
  // an editor viewport is looking. Only SimEntity views need resolving against the world.
  out.views.reserve(views.size());
  for (const auto& request : views) {
    auto view = ViewSnapshot{
      .view_id = request.view_id,
      .source = request.source,
      .camera_entity = request.camera_entity,
      .viewport_size = request.viewport_size,
      .viewport_offset = request.viewport_offset,
      .camera = request.camera,
    };

    if (request.source == SimCameraSource::SimEntity) {
      if (auto camera = resolve(request.camera_entity)) {
        view.camera = camera_data_from_component(*camera);
      }
    }

    out.views.emplace_back(view);
  }

  // Nothing asked for a view but the scene has a camera: emit one anyway, so a headless tick still
  // produces something meaningful.
  if (out.views.empty() && fallback_camera) {
    out.views.emplace_back(
      ViewSnapshot{
        .view_id = SimViewID::Invalid,
        .source = SimCameraSource::SimEntity,
        .camera_entity = fallback_entity,
        .camera = camera_data_from_component(*fallback_camera),
      }
    );
  }
}

auto SceneExtractor::extract_lights_and_sky(this SceneExtractor& self, Scene& scene, FrameSnapshot& out) -> void {
  ZoneScoped;

  self.light_query.each([&self, &out](flecs::entity e, const TransformComponent&, const LightComponent& lc) {
    if (!e.enabled()) {
      return;
    }

    const glm::mat4 world_transform = Scene::get_world_transform(e);
    const glm::vec3 world_position = world_transform[3];
    const glm::vec3 world_forward = glm::normalize(glm::mat3(world_transform) * glm::vec3(0.0f, 0.0f, -1.0f));

    if (lc.type == LightComponent::LightType::Directional) {
      out.flags |= GPU::SceneFlags::HasDirectionalLight;
      out.directional_light.color = lc.color;
      out.directional_light.intensity = lc.intensity;
      out.directional_light.direction = world_forward;
      out.sun_direction_changed = world_forward != self.previous_sun_direction;
      self.previous_sun_direction = world_forward;
      out.first_clipmap_width = lc.first_clipmap_width;
      out.clipmap_selection_bias = lc.clipmap_selection_bias;
      out.directional_light_cast_shadows = lc.cast_shadows;
    } else if (out.lights.size() < GPU::MAX_LIGHTS) {
      const auto kind = lc.type == LightComponent::LightType::Spot ? GPU::LightKind::Spot : GPU::LightKind::Point;
      const auto direction = lc.type == LightComponent::LightType::Spot ? world_forward : glm::vec3(0.0f);

      out.lights.emplace_back(
        GPU::Light{
          .position = world_position,
          .intensity = lc.intensity,
          .color = lc.color,
          .range = lc.radius,
          .direction = direction,
          .inner_cone_angle = lc.inner_cone_angle,
          .outer_cone_angle = lc.outer_cone_angle,
          .kind = kind,
        }
      );
    }

    // Atmosphere and sky ride along on the light entity, as they did before the split.
    if (const auto* atmos_info = e.try_get<AtmosphereComponent>()) {
      out.flags |= GPU::SceneFlags::HasAtmosphere;

      out.atmosphere.rayleigh_scatter = atmos_info->rayleigh_scattering * 1e-3f;
      out.atmosphere.rayleigh_density = atmos_info->rayleigh_density;
      out.atmosphere.mie_scatter = atmos_info->mie_scattering * 1e-3f;
      out.atmosphere.mie_density = atmos_info->mie_density;
      out.atmosphere.mie_extinction = atmos_info->mie_extinction * 1e-3f;
      out.atmosphere.mie_asymmetry = atmos_info->mie_asymmetry;
      out.atmosphere.ozone_absorption = atmos_info->ozone_absorption * 1e-3f;
      out.atmosphere.ozone_height = atmos_info->ozone_height;
      out.atmosphere.ozone_thickness = atmos_info->ozone_thickness;
      out.atmosphere.aerial_perspective_start_km = atmos_info->aerial_perspective_start_km;
      out.atmosphere.aerial_perspective_exposure = atmos_info->aerial_perspective_exposure;
    }

    if (const auto* sky_info = e.try_get<SkyComponent>()) {
      out.flags |= GPU::SceneFlags::HasSky;

      out.sky.solid_color = sky_info->solid_color;
      out.sky.ambient_color = sky_info->ambient_color;
      out.sky.has_texture = static_cast<bool>(sky_info->texture);
    }
  });
}

auto SceneExtractor::extract_post_process(this SceneExtractor& self, FrameSnapshot& out) -> void {
  ZoneScoped;

  self.auto_exposure_query.each([&out](flecs::entity, const AutoExposureComponent& c) {
    out.flags |= GPU::SceneFlags::HasEyeAdaptation;
    out.eye_adaptation.max_exposure = c.max_exposure;
    out.eye_adaptation.min_exposure = c.min_exposure;
    out.eye_adaptation.adaptation_speed = c.adaptation_speed;
    out.eye_adaptation.ev100_bias = c.ev100_bias;
  });

  self.vignette_query.each([&out](flecs::entity, const VignetteComponent& c) {
    out.post_process.vignette_amount = c.amount;
    out.flags |= GPU::SceneFlags::HasVignette;
  });

  self.chromatic_aberration_query.each([&out](flecs::entity, const ChromaticAberrationComponent& c) {
    out.post_process.chromatic_aberration_amount = c.amount;
    out.flags |= GPU::SceneFlags::HasChromaticAberration;
  });

  self.film_grain_query.each([&out](flecs::entity, const FilmGrainComponent& c) {
    out.post_process.film_grain_amount = c.amount;
    out.post_process.film_grain_scale = c.scale;
    // film_grain_seed is left to the renderer: it is derived from the frame counter, which is a
    // property of presentation rather than of the world.
    out.flags |= GPU::SceneFlags::HasFilmGrain;
  });

  self.tonemap_query.each([&out](flecs::entity, const TonemappingComponent& c) { out.tonemap_type = c.tonemap_type; });
}

auto SceneExtractor::extract_sprites(this SceneExtractor& self, Scene& scene, FrameSnapshot& out) -> void {
  ZoneScoped;

  // Material UUIDs rather than resolved indices, and positions rather than distances: sorting is
  // view-dependent, so the client does it once it knows which camera it is drawing for.
  self.sprite_query.each([&scene, &out](flecs::entity e, const TransformComponent& tc, const SpriteComponent& comp) {
    auto transform_id = scene.get_entity_transform_id(e);
    if (!transform_id) {
      OX_LOG_WARN("No registered transform for sprite entity: {}", e.name().c_str());
      return;
    }

    u16 flags = 0;
    if (comp.sort_y)
      flags |= GPU::RENDER_FLAGS_2D_SORT_Y;
    if (comp.flip_x)
      flags |= GPU::RENDER_FLAGS_2D_FLIP_X;

    out.sprites.emplace_back(
      Sprite2DSnapshot{
        .flags = flags,
        .position_y = tc.position.y,
        .position_z = tc.position.z,
        .transform_index = SlotMap_decode_id(*transform_id).index,
        .material_uuid = comp.material,
      }
    );
  });

  self.particle_query.each([&scene,
                            &out](flecs::entity e, const TransformComponent& tc, const ParticleComponent& comp) {
    if (comp.life_remaining <= 0.0f) {
      return;
    }

    const auto* particle_system = e.parent().try_get<ParticleSystemComponent>();
    if (!particle_system) {
      return;
    }

    auto transform_id = scene.get_entity_transform_id(e);
    if (!transform_id) {
      OX_LOG_WARN("No registered transform for particle entity: {}", e.name().c_str());
      return;
    }

    out.sprites.emplace_back(
      Sprite2DSnapshot{
        .flags = GPU::RENDER_FLAGS_2D_SORT_Y,
        .position_y = tc.position.y,
        .position_z = tc.position.z,
        .transform_index = SlotMap_decode_id(*transform_id).index,
        .material_uuid = particle_system->material,
      }
    );
  });
}
} // namespace ox
