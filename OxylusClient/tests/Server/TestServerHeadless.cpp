#include <atomic>
#include <gtest/gtest.h>

#include "Asset/AssetManager.hpp"
#include "Core/EventSystem.hpp"
#include "Core/JobManager.hpp"
#include "Core/UUID.hpp"
#include "Core/VFS.hpp"

// This binary links ox::OxylusServerLib alone - no vuk, no SDL, no ImGui, no RmlUi. It exists as much to
// fail to link as to pass: if the simulation half ever acquires a dependency on the presentation
// half, this is where it surfaces.

namespace ox {
struct TestPing {
  u32 value = 0;
};

TEST(SimHeadless, EventSystemDispatchesToSubscriber) {
  auto events = EventSystem{};
  ASSERT_TRUE(events.init().has_value());

  auto received = 0_u32;
  const auto id = events.subscribe<TestPing>([&received](const TestPing& event) { received = event.value; });
  ASSERT_TRUE(id.has_value());

  ASSERT_TRUE(events.emit(TestPing{.value = 42}).has_value());
  EXPECT_EQ(received, 42_u32);

  static_cast<void>(events.deinit());
}

TEST(SimHeadless, EventSystemStopsDispatchingAfterUnsubscribe) {
  auto events = EventSystem{};
  ASSERT_TRUE(events.init().has_value());

  auto call_count = 0_u32;
  const auto id = events.subscribe<TestPing>([&call_count](const TestPing&) { call_count += 1; });
  ASSERT_TRUE(id.has_value());

  ASSERT_TRUE(events.emit(TestPing{.value = 1}).has_value());
  EXPECT_EQ(call_count, 1_u32);

  ASSERT_TRUE(events.unsubscribe<TestPing>(*id).has_value());
  static_cast<void>(events.emit(TestPing{.value = 2}));
  EXPECT_EQ(call_count, 1_u32);

  static_cast<void>(events.deinit());
}

TEST(SimHeadless, EventSystemRejectsWorkAfterShutdown) {
  auto events = EventSystem{};
  ASSERT_TRUE(events.init().has_value());
  static_cast<void>(events.deinit());

  EXPECT_FALSE(events.subscribe<TestPing>([](const TestPing&) {}).has_value());
  EXPECT_FALSE(events.emit(TestPing{.value = 1}).has_value());
}

TEST(SimHeadless, JobManagerRunsSubmittedWork) {
  auto jobs = JobManager{};
  ASSERT_TRUE(jobs.init().has_value());

  auto counter = std::atomic<u32>{0};
  constexpr auto job_count = 64_u32;
  for (auto i = 0_u32; i < job_count; ++i) {
    jobs.submit(Job::create([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }));
  }
  jobs.wait();

  EXPECT_EQ(counter.load(std::memory_order_relaxed), job_count);

  static_cast<void>(jobs.deinit());
}

TEST(SimHeadless, VirtualFileSystemResolvesMountedDirectory) {
  auto vfs = VFS{};
  vfs.mount_dir(VFS::APP_DIR, "Resources");
  EXPECT_TRUE(vfs.is_mounted_dir(VFS::APP_DIR));

  const auto resolved = vfs.resolve_physical_dir(VFS::APP_DIR, "shaders/foo.slang").string();
  EXPECT_NE(resolved.find("Resources"), std::string::npos);

  vfs.unmount_dir(VFS::APP_DIR);
  EXPECT_FALSE(vfs.is_mounted_dir(VFS::APP_DIR));
}

TEST(SimHeadless, UUIDsAreUniqueAndRoundTripThroughString) {
  const auto a = UUID::generate_random();
  const auto b = UUID::generate_random();
  EXPECT_NE(a, b);

  const auto parsed = UUID::from_string(a.str());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, a);
}
// The asset registry is simulation-side, but materialising a model or a texture is GPU work, so
// the client installs its loaders at startup. A headless process installs none and must degrade
// rather than reach for a renderer that is not there - that contract is what lets AssetManager
// compile into this target at all.
TEST(SimHeadless, AssetLoadersAreAbsentByDefaultAndDispatchOnceInstalled) {
  auto assets = AssetManager{};

  EXPECT_EQ(assets.get_texture_extent(UUID(nullptr)), glm::uvec2(0, 0));

  static auto extent_calls = 0;
  extent_calls = 0;

  // Captureless, so it converts to the plain function pointer the hook stores.
  AssetManager::install_loaders({
    .texture_extent = [](AssetManager&, const UUID&) {
      ++extent_calls;
      return glm::uvec2(64, 32);
    },
  });

  EXPECT_EQ(assets.get_texture_extent(UUID(nullptr)), glm::uvec2(64, 32));
  EXPECT_EQ(extent_calls, 1);

  AssetManager::install_loaders({});
  EXPECT_EQ(assets.get_texture_extent(UUID(nullptr)), glm::uvec2(0, 0));
}
} // namespace ox
