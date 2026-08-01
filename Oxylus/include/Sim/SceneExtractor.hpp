#pragma once

#include <flecs.h>
#include <span>

#include "Scene/Components.hpp"
#include "Sim/FrameSnapshot.hpp"

namespace ox {
class Scene;

// Turns the live ECS into a FrameSnapshot once per simulation tick.
//
// The queries are built once and kept as members. RendererInstance::update used to call
// query_builder(...).build() eight times per frame, which is both a per-frame cost and the reason
// the renderer needed to know what a flecs world was.
class SceneExtractor {
public:
  SceneExtractor() = default;

  // Builds the queries. Separate from the constructor because Scene owns an extractor by value and
  // its world is not ready at construction time.
  auto init(this SceneExtractor& self, flecs::world& world) -> void;
  auto deinit(this SceneExtractor& self) -> void;

  // Runs after every system has run for the tick, so it observes settled state. `views` is what the
  // client asked for this frame; the extractor resolves each one against the scene.
  auto extract(this SceneExtractor& self, Scene& scene, std::span<const ViewRequest> views, FrameSnapshot& out) -> void;

private:
  auto extract_views(this SceneExtractor& self, Scene& scene, std::span<const ViewRequest> views, FrameSnapshot& out)
    -> void;
  auto extract_lights_and_sky(this SceneExtractor& self, Scene& scene, FrameSnapshot& out) -> void;
  auto extract_post_process(this SceneExtractor& self, FrameSnapshot& out) -> void;
  auto extract_sprites(this SceneExtractor& self, Scene& scene, FrameSnapshot& out) -> void;
  auto extract_geometry(this SceneExtractor& self, Scene& scene, FrameSnapshot& out) -> void;

  bool initialized = false;

  flecs::query<const TransformComponent, const CameraComponent> camera_query = {};
  flecs::query<const TransformComponent, const LightComponent> light_query = {};
  flecs::query<const TransformComponent, const SpriteComponent> sprite_query = {};
  flecs::query<const TransformComponent, const ParticleComponent> particle_query = {};
  flecs::query<const AutoExposureComponent> auto_exposure_query = {};
  flecs::query<const VignetteComponent> vignette_query = {};
  flecs::query<const ChromaticAberrationComponent> chromatic_aberration_query = {};
  flecs::query<const FilmGrainComponent> film_grain_query = {};
  flecs::query<const TonemappingComponent> tonemap_query = {};

  glm::vec3 previous_sun_direction = {};

  // Scratch, kept across ticks so the per-frame rebuild does not reallocate.
  ankerl::unordered_dense::map<u32, EntityHandle> slot_to_entity = {};
};
} // namespace ox
