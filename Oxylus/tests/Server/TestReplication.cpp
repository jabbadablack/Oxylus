#include <gtest/gtest.h>

#include "Asset/AssetManager.hpp"
#include "Audio/AudioEngine.hpp"
#include "Core/EventSystem.hpp"
#include "Core/JobManager.hpp"
#include "Core/VFS.hpp"
#include "Physics/Physics.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneSnapshot.hpp"
#include "Scripting/LuaManager.hpp"
#include "Server/Server.hpp"

// Replication, proven without a socket in sight.
//
// take_snapshot encodes a world; apply_scene_state rebuilds it. Everything the server/client split
// depends on rests on those two agreeing, so this exercises them against each other directly - and
// it links ox::OxylusServerLib alone, so neither side can quietly reach for the renderer.

namespace ox {
class ReplicationTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(job_manager.init().has_value());
    ASSERT_TRUE(event_system.init().has_value());

    server = std::make_unique<Server>(vfs, job_manager, event_system, timestep);
    server->add<LuaManager>();
    server->add<AssetManager>();
    server->add<AudioEngine>();
    server->add<Physics>();
    ASSERT_TRUE(server->init().has_value());

    authoritative = std::make_unique<Scene>("authoritative");
    replica = std::make_unique<Scene>("replica");
  }

  void TearDown() override {
    replica.reset();
    authoritative.reset();
    static_cast<void>(server->deinit());
    server.reset();
    static_cast<void>(event_system.deinit());
    static_cast<void>(job_manager.deinit());
  }

  // Snapshots the authoritative world and applies it to the replica, the way the wire would.
  auto replicate(this ReplicationTest& self) -> void {
    auto state = SceneState{};
    SceneSnapshotBuilder::take_snapshot(self.authoritative->world, state);
    apply_scene_state(self.replica->world, state);
  }

  auto replica_entity(this ReplicationTest& self, flecs::entity source) -> flecs::entity {
    return self.replica->world.entity(source.id());
  }

  VFS vfs = {};
  JobManager job_manager = {};
  EventSystem event_system = {};
  Timestep timestep = {};
  std::unique_ptr<Server> server = nullptr;
  std::unique_ptr<Scene> authoritative = nullptr;
  std::unique_ptr<Scene> replica = nullptr;
};

TEST_F(ReplicationTest, EntityIdsSurviveTheRoundTrip) {
  auto source = authoritative->create_entity("box");
  source.set<TransformComponent>({.position = glm::vec3(1.f, 2.f, 3.f)});

  replicate();

  // The same id, not merely an equivalent entity. Commands name entities by handle, so this is the
  // property the whole command channel rests on.
  auto mirrored = replica_entity(source);
  ASSERT_TRUE(mirrored.is_alive());
  EXPECT_EQ(mirrored.id(), source.id());
}

TEST_F(ReplicationTest, ComponentValuesArrive) {
  auto source = authoritative->create_entity("light");
  source.set<TransformComponent>({.position = glm::vec3(4.f, 5.f, 6.f)});
  source.set<LightComponent>({
    .type = LightComponent::LightType::Point,
    .color = glm::vec3(0.25f, 0.5f, 0.75f),
    .intensity = 12.5f,
  });

  replicate();

  auto mirrored = replica_entity(source);
  ASSERT_TRUE(mirrored.is_alive());
  ASSERT_TRUE(mirrored.has<TransformComponent>());
  ASSERT_TRUE(mirrored.has<LightComponent>());

  EXPECT_FLOAT_EQ(mirrored.get<TransformComponent>().position.y, 5.f);

  const auto& light = mirrored.get<LightComponent>();
  EXPECT_EQ(light.type, LightComponent::LightType::Point);
  EXPECT_FLOAT_EQ(light.intensity, 12.5f);
  EXPECT_FLOAT_EQ(light.color.g, 0.5f);
}

TEST_F(ReplicationTest, NameAndParentAndEnabledArrive) {
  auto parent = authoritative->create_entity("parent");
  parent.set<TransformComponent>({});

  auto child = authoritative->create_entity("child");
  child.set<TransformComponent>({});
  child.child_of(parent);

  auto disabled = authoritative->create_entity("disabled");
  disabled.set<TransformComponent>({});
  disabled.disable();

  replicate();

  // None of these live in a component, so a component-only snapshot would silently lose all three.
  EXPECT_STREQ(replica_entity(parent).name().c_str(), "parent");
  EXPECT_EQ(replica_entity(child).parent().id(), parent.id());
  EXPECT_FALSE(replica_entity(disabled).enabled());
  EXPECT_TRUE(replica_entity(parent).enabled());
}

// The reason the blob encoder exists: a memcpy of this component would ship a std::vector's heap
// pointer to another process.
TEST_F(ReplicationTest, ComponentWithHeapStorageSurvives) {
  auto source = authoritative->create_entity("particles");
  source.set<TransformComponent>({});
  source.set<ParticleSystemComponent>({.duration = 7.5f, .looping = false});

  replicate();

  auto mirrored = replica_entity(source);
  ASSERT_TRUE(mirrored.has<ParticleSystemComponent>());
  EXPECT_FLOAT_EQ(mirrored.get<ParticleSystemComponent>().duration, 7.5f);
  EXPECT_FALSE(mirrored.get<ParticleSystemComponent>().looping);
}

TEST_F(ReplicationTest, DeltaCarriesOnlyWhatChangedAndConverges) {
  auto a = authoritative->create_entity("a");
  a.set<TransformComponent>({.position = glm::vec3(1.f)});
  auto b = authoritative->create_entity("b");
  b.set<TransformComponent>({.position = glm::vec3(2.f)});

  auto builder = SceneSnapshotBuilder{};
  SceneSnapshotBuilder::take_snapshot(authoritative->world, builder.current());
  apply_scene_state(replica->world, builder.current());
  builder.ack(builder.current_sequence);
  builder.advance();

  // Move only `a`.
  a.set<TransformComponent>({.position = glm::vec3(9.f)});
  SceneSnapshotBuilder::take_snapshot(authoritative->world, builder.current());

  const auto delta = builder.delta();
  EXPECT_TRUE(delta.entities.contains(a.id()));
  EXPECT_FALSE(delta.entities.contains(b.id())) << "an untouched entity should not be in the delta";

  apply_scene_state(replica->world, delta);
  EXPECT_FLOAT_EQ(replica_entity(a).get<TransformComponent>().position.x, 9.f);
  EXPECT_FLOAT_EQ(replica_entity(b).get<TransformComponent>().position.x, 2.f);
}

TEST_F(ReplicationTest, RenameIsCarriedByTheDelta) {
  auto source = authoritative->create_entity("before");
  source.set<TransformComponent>({});

  auto builder = SceneSnapshotBuilder{};
  SceneSnapshotBuilder::take_snapshot(authoritative->world, builder.current());
  apply_scene_state(replica->world, builder.current());
  builder.ack(builder.current_sequence);
  builder.advance();

  // A rename touches no component at all, so it only survives if identity is diffed separately.
  source.set_name("after");
  SceneSnapshotBuilder::take_snapshot(authoritative->world, builder.current());

  const auto delta = builder.delta();
  ASSERT_TRUE(delta.entities.contains(source.id()));

  apply_scene_state(replica->world, delta);
  EXPECT_STREQ(replica_entity(source).name().c_str(), "after");
}

TEST_F(ReplicationTest, DestroyedEntitiesAreRemovedFromTheReplica) {
  auto doomed = authoritative->create_entity("doomed");
  doomed.set<TransformComponent>({});
  const auto doomed_id = doomed.id();

  auto builder = SceneSnapshotBuilder{};
  SceneSnapshotBuilder::take_snapshot(authoritative->world, builder.current());
  apply_scene_state(replica->world, builder.current());
  ASSERT_TRUE(replica->world.is_alive(doomed_id));

  builder.ack(builder.current_sequence);
  builder.advance();

  doomed.destruct();
  SceneSnapshotBuilder::take_snapshot(authoritative->world, builder.current());

  const auto delta = builder.delta();
  EXPECT_TRUE(delta.removed_entities.contains(doomed_id));

  apply_scene_state(replica->world, delta);
  EXPECT_FALSE(replica->world.is_alive(doomed_id));
}

// Not an assertion so much as a measurement: Phase 5 sends these at 60 Hz, and the plan says to
// find out how big they are before trusting that.
TEST_F(ReplicationTest, ReportsSnapshotSize) {
  for (auto i = 0_u32; i < 100_u32; ++i) {
    auto e = authoritative->create_entity(fmt::format("entity_{}", i));
    e.set<TransformComponent>({.position = glm::vec3(static_cast<f32>(i))});
    e.set<MeshComponent>({});
    e.set<LightComponent>({});
  }

  auto state = SceneState{};
  SceneSnapshotBuilder::take_snapshot(authoritative->world, state);

  auto component_bytes = 0_sz;
  auto component_count = 0_sz;
  for (const auto& [entity_id, entity_state] : state.entities) {
    for (const auto& [component_id, component_state] : entity_state.components) {
      component_bytes += component_state.buffer.size();
      component_count += 1;
    }
  }

  fmt::println(
    "  [replication] {} entities, {} components, {} bytes of component payload (~{} B/entity)",
    state.entities.size(),
    component_count,
    component_bytes,
    component_bytes / std::max(state.entities.size(), 1_sz)
  );

  EXPECT_GE(state.entities.size(), 100_sz);
  EXPECT_GT(component_bytes, 0_sz);
}
} // namespace ox
