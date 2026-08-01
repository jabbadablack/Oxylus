#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Core/App.hpp"
#include "Core/DefaultModules.hpp"
#include "Scene/ComponentBlob.hpp"
#include "Scene/Scene.hpp"
#include "Server/ServerCommand.hpp"

// Commands exist so the editor can undo without holding raw pointers into ECS storage. What matters
// here is that each one applies, and that its inverse restores what was there before.

namespace ox {
class ServerCommandTest : public ::testing::Test {
protected:
  void SetUp() override {
    static char arg0[] = "testarg";
    static char* test_argv[] = {arg0, nullptr};
    auto argc = 1;
    auto** argv = test_argv;

    app = std::make_unique<App>(argc, argv);
    app->with_name("ServerCommandTest").with<LuaManager>().with<AssetManager>().with<AudioEngine>().with<Physics>();
    app->init();

    scene = std::make_unique<Scene>("command_test");
  }

  void TearDown() override {
    scene.reset();
    app->stop();
    app.reset();
  }

  auto apply(ServerCommandPayload payload) -> EntityHandle {
    return apply_command(*scene, ServerCommand{.payload = std::move(payload)});
  }

  auto make_entity(const std::string& name) -> flecs::entity { return scene->create_entity(name, true); }

  std::unique_ptr<App> app = nullptr;
  std::unique_ptr<Scene> scene = nullptr;
};

TEST_F(ServerCommandTest, DestroyEntity) {
  auto entity = make_entity("doomed");
  const auto handle = static_cast<EntityHandle>(entity.id());

  apply(CmdDestroyEntity{.entity = handle});
  EXPECT_FALSE(scene->world.is_alive(static_cast<flecs::entity_t>(handle)));
}

TEST_F(ServerCommandTest, DestroyThenRestoreBringsTheEntityBack) {
  auto entity = make_entity("restore_me");
  entity.set<TransformComponent>({.position = glm::vec3(4.f, 5.f, 6.f)});

  auto writer = JsonWriter{};
  Scene::entity_to_json(writer, entity);
  const auto json = writer.stream.str();

  apply(CmdDestroyEntity{.entity = static_cast<EntityHandle>(entity.id())});
  ASSERT_FALSE(scene->world.is_alive(entity.id()));

  const auto restored = apply(CmdRestoreEntity{.serialized_json = json});
  ASSERT_NE(restored, EntityHandle::Invalid);

  auto restored_entity = scene->world.entity(static_cast<flecs::entity_t>(restored));
  ASSERT_TRUE(restored_entity.is_alive());
  EXPECT_FLOAT_EQ(restored_entity.get<TransformComponent>().position.x, 4.f);
  EXPECT_FLOAT_EQ(restored_entity.get<TransformComponent>().position.z, 6.f);
}

TEST_F(ServerCommandTest, SetTransformAndItsInverse) {
  auto entity = make_entity("e");
  entity.set<TransformComponent>({.position = glm::vec3(1.f, 2.f, 3.f)});
  const auto handle = static_cast<EntityHandle>(entity.id());

  const auto before = entity.get<TransformComponent>();

  apply(CmdSetTransform{.entity = handle, .position = glm::vec3(9.f, 8.f, 7.f), .scale = glm::vec3(2.f)});
  EXPECT_FLOAT_EQ(entity.get<TransformComponent>().position.x, 9.f);
  EXPECT_FLOAT_EQ(entity.get<TransformComponent>().scale.x, 2.f);

  apply(
    CmdSetTransform{
      .entity = handle,
      .position = before.position,
      .rotation = before.rotation,
      .scale = before.scale,
    }
  );
  EXPECT_FLOAT_EQ(entity.get<TransformComponent>().position.x, 1.f);
  EXPECT_FLOAT_EQ(entity.get<TransformComponent>().position.z, 3.f);
}

TEST_F(ServerCommandTest, ComponentBlobRoundTripsThroughReflection) {
  auto entity = make_entity("e");
  entity.set<LightComponent>({
    .type = LightComponent::LightType::Point,
    .color = glm::vec3(0.25f, 0.5f, 0.75f),
    .intensity = 12.5f,
  });

  const auto light_id = scene->world.component<LightComponent>().raw_id();

  auto blob = std::vector<u8>{};
  ASSERT_TRUE(write_component_blob(entity, light_id, blob));
  ASSERT_FALSE(blob.empty());

  entity.set<LightComponent>({.type = LightComponent::LightType::Directional, .intensity = 0.f});

  apply(
    CmdSetComponent{
      .entity = static_cast<EntityHandle>(entity.id()),
      .state = ComponentState{.id = light_id, .buffer = blob},
    }
  );

  const auto& restored = entity.get<LightComponent>();
  EXPECT_EQ(restored.type, LightComponent::LightType::Point);
  EXPECT_FLOAT_EQ(restored.intensity, 12.5f);
  EXPECT_FLOAT_EQ(restored.color.g, 0.5f);
}

// The reason the blob path exists at all: a memcpy of this component would copy a std::vector's
// heap pointer into a buffer that later gets replayed.
TEST_F(ServerCommandTest, BlobOfAComponentWithHeapStorageIsNotAByteCopy) {
  auto entity = make_entity("e");
  entity.set<ParticleSystemComponent>({});

  auto blob = std::vector<u8>{};
  ASSERT_TRUE(write_component_blob(entity, scene->world.component<ParticleSystemComponent>().raw_id(), blob));
  EXPECT_NE(blob.size(), sizeof(ParticleSystemComponent));
}

TEST_F(ServerCommandTest, CommandsOnDeadEntitiesAreIgnored) {
  auto entity = make_entity("doomed");
  const auto handle = static_cast<EntityHandle>(entity.id());
  apply(CmdDestroyEntity{.entity = handle});

  // None of these should crash or resurrect anything.
  apply(CmdSetTransform{.entity = handle});
  apply(CmdSetComponent{.entity = handle});
  apply(CmdDestroyEntity{.entity = handle});

  EXPECT_FALSE(scene->world.is_alive(static_cast<flecs::entity_t>(handle)));
}
} // namespace ox
