#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Asset/AssetManager.hpp"
#include "Audio/AudioEngine.hpp"
#include "Core/Enum.hpp"
#include "Core/EventSystem.hpp"
#include "Core/JobManager.hpp"
#include "Core/VFS.hpp"
#include "Physics/Physics.hpp"
#include "Scene/Scene.hpp"
#include "Scripting/LuaManager.hpp"
#include "Server/SceneExtractor.hpp"
#include "Server/Server.hpp"

// Exercises the extract that replaced the eight live ECS queries RendererInstance::update used to
// run every frame.
//
// This binary links ox::OxylusServerLib alone - there is no App here, no window, no render context and
// no vuk anywhere on the link line. A Scene is built, populated and extracted entirely through
// Server. That is the whole point of the split, so if this ever needs the presentation target
// again, the boundary has regressed.

namespace ox {
class SceneExtractorTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(job_manager.init().has_value());
    ASSERT_TRUE(event_system.init().has_value());

    // The shared services are owned by the test rather than by an App. LuaManager is needed
    // because component reflection registers Lua bindings as it imports CoreComponentsModule; no
    // Renderer exists to register, and Scene::init guards on that.
    sim_host = std::make_unique<Server>(vfs, job_manager, event_system, timestep);
    sim_host->add<LuaManager>();
    sim_host->add<AssetManager>();
    sim_host->add<AudioEngine>();
    sim_host->add<Physics>();
    ASSERT_TRUE(sim_host->init().has_value());

    scene = std::make_unique<Scene>("extract_test");
  }

  void TearDown() override {
    scene.reset();
    static_cast<void>(sim_host->deinit());
    sim_host.reset();
    static_cast<void>(event_system.deinit());
    static_cast<void>(job_manager.deinit());
  }

  auto extract() -> const FrameSnapshot& {
    scene->extractor.extract(*scene, scene->view_requests, scene->frame_snapshot);
    return scene->frame_snapshot;
  }

  VFS vfs = {};
  JobManager job_manager = {};
  EventSystem event_system = {};
  Timestep timestep = {};
  std::unique_ptr<Server> sim_host = nullptr;
  std::unique_ptr<Scene> scene = nullptr;
};

TEST_F(SceneExtractorTest, EmptySceneProducesEmptySnapshot) {
  const auto& snapshot = extract();

  EXPECT_TRUE(snapshot.lights.empty());
  EXPECT_TRUE(snapshot.sprites.empty());
  EXPECT_TRUE(snapshot.views.empty());
}

TEST_F(SceneExtractorTest, PointAndSpotLightsAreExtractedButDirectionalIsNot) {
  // Directional lights are the sun, carried in `directional_light` rather than the light array.
  auto sun = scene->create_entity("sun");
  sun.set<TransformComponent>({});
  sun.set<LightComponent>({.type = LightComponent::LightType::Directional, .intensity = 7.f});

  auto point = scene->create_entity("point");
  point.set<TransformComponent>({.position = glm::vec3(1.f, 2.f, 3.f)});
  point.set<LightComponent>({.type = LightComponent::LightType::Point, .intensity = 3.f});

  auto spot = scene->create_entity("spot");
  spot.set<TransformComponent>({});
  spot.set<LightComponent>({.type = LightComponent::LightType::Spot});

  const auto& snapshot = extract();

  EXPECT_EQ(snapshot.lights.size(), 2_sz);
  EXPECT_TRUE(static_cast<bool>(snapshot.flags & GPU::SceneFlags::HasDirectionalLight));
  EXPECT_FLOAT_EQ(snapshot.directional_light.intensity, 7.f);

  const auto point_it = std::ranges::find_if(snapshot.lights, [](const GPU::Light& light) {
    return light.kind == GPU::LightKind::Point;
  });
  ASSERT_NE(point_it, snapshot.lights.end());
  EXPECT_FLOAT_EQ(point_it->intensity, 3.f);
  EXPECT_FLOAT_EQ(point_it->position.x, 1.f);
  EXPECT_FLOAT_EQ(point_it->position.y, 2.f);
  EXPECT_FLOAT_EQ(point_it->position.z, 3.f);
}

TEST_F(SceneExtractorTest, DisabledLightsAreSkipped) {
  auto point = scene->create_entity("point");
  point.set<TransformComponent>({});
  point.set<LightComponent>({.type = LightComponent::LightType::Point});

  ASSERT_EQ(extract().lights.size(), 1_sz);

  point.disable();
  EXPECT_TRUE(extract().lights.empty());
}

TEST_F(SceneExtractorTest, ClientSuppliedViewIsPassedThroughUntouched) {
  auto camera = GPU::CameraData{};
  camera.near_clip = 0.25f;
  camera.far_clip = 500.f;
  camera.fov = 42.f;

  scene->view_requests.push_back(
    ViewRequest{
      .view_id = static_cast<ServerViewID>(3),
      .source = ServerCameraSource::ClientSupplied,
      .viewport_size = glm::uvec2(800, 600),
      .camera = camera,
    }
  );

  const auto& snapshot = extract();

  ASSERT_EQ(snapshot.views.size(), 1_sz);
  const auto& view = snapshot.views.front();
  EXPECT_EQ(view.view_id, static_cast<ServerViewID>(3));
  EXPECT_EQ(view.source, ServerCameraSource::ClientSupplied);
  EXPECT_EQ(view.viewport_size, glm::uvec2(800, 600));
  EXPECT_FLOAT_EQ(view.camera.near_clip, 0.25f);
  EXPECT_FLOAT_EQ(view.camera.far_clip, 500.f);
  EXPECT_FLOAT_EQ(view.camera.fov, 42.f);
}

TEST_F(SceneExtractorTest, SimEntityViewResolvesAgainstTheSceneCamera) {
  auto camera_entity = scene->create_entity("camera");
  camera_entity.set<TransformComponent>({.position = glm::vec3(0.f, 5.f, 0.f)});
  camera_entity.set<CameraComponent>({.fov = 33.f, .far_clip = 123.f, .near_clip = 0.5f});

  scene->view_requests.push_back(
    ViewRequest{
      .view_id = static_cast<ServerViewID>(1),
      .source = ServerCameraSource::SimEntity,
      .camera_entity = static_cast<EntityHandle>(camera_entity.id()),
      .viewport_size = glm::uvec2(1280, 720),
    }
  );

  const auto& snapshot = extract();

  ASSERT_EQ(snapshot.views.size(), 1_sz);
  EXPECT_FLOAT_EQ(snapshot.views[0].camera.fov, 33.f);
  EXPECT_FLOAT_EQ(snapshot.views[0].camera.far_clip, 123.f);
}

TEST_F(SceneExtractorTest, PostProcessComponentsSetTheirFlags) {
  auto e = scene->create_entity("post");
  e.set<TransformComponent>({});
  e.set<VignetteComponent>({.amount = 0.5f});
  e.set<FilmGrainComponent>({.amount = 0.25f, .scale = 2.f});
  e.set<TonemappingComponent>({.tonemap_type = GPU::TonemapType::ACES});

  const auto& snapshot = extract();

  EXPECT_TRUE(static_cast<bool>(snapshot.flags & GPU::SceneFlags::HasVignette));
  EXPECT_TRUE(static_cast<bool>(snapshot.flags & GPU::SceneFlags::HasFilmGrain));
  EXPECT_FLOAT_EQ(snapshot.post_process.vignette_amount, 0.5f);
  EXPECT_FLOAT_EQ(snapshot.post_process.film_grain_scale, 2.f);
  EXPECT_EQ(snapshot.tonemap_type, GPU::TonemapType::ACES);

  // The film grain seed is derived from the renderer's frame counter, so the extract leaves it be.
  EXPECT_EQ(snapshot.post_process.film_grain_seed, 0_u32);
}

TEST_F(SceneExtractorTest, ExtractIsIdempotentAcrossRepeatedCalls) {
  auto point = scene->create_entity("point");
  point.set<TransformComponent>({});
  point.set<LightComponent>({.type = LightComponent::LightType::Point});

  ASSERT_EQ(extract().lights.size(), 1_sz);
  // clear() resizes to zero rather than reallocating, so a second extract must not accumulate.
  EXPECT_EQ(extract().lights.size(), 1_sz);
  EXPECT_EQ(extract().lights.size(), 1_sz);
}

TEST_F(SceneExtractorTest, ViewportExtentFallsBackWhenNoViewIsRequested) {
  auto camera_entity = scene->create_entity("camera");
  camera_entity.set<TransformComponent>({});
  camera_entity.set<CameraComponent>({});

  EXPECT_EQ(scene->viewport_extent_for(camera_entity), glm::uvec2(1920, 1080));

  scene->view_requests.push_back(
    ViewRequest{
      .camera_entity = static_cast<EntityHandle>(camera_entity.id()),
      .viewport_size = glm::uvec2(640, 480),
    }
  );

  EXPECT_EQ(scene->viewport_extent_for(camera_entity), glm::uvec2(640, 480));
}
} // namespace ox
