#pragma once

// clang-format off
#include <expected>

#include "Physics/PhysicsDebugRenderer.hpp"
#include "Physics/PhysicsInterfaces.hpp"

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

namespace ox {
class RayCast;

class Physics {
public:
  constexpr static auto MODULE_NAME = "Physics";

  // Simulation-side: App::with<T>() and App::mod<T>() route this to SimHost.
  constexpr static bool SIM_MODULE = true;

  static constexpr uint32_t MAX_BODIES = 1024;
  static constexpr uint32_t MAX_BODY_PAIRS = 1024;
  static constexpr uint32_t MAX_CONTACT_CONSTRAINS = 1024;
  BPLayerInterfaceImpl layer_interface;
  ObjectVsBroadPhaseLayerFilterImpl object_vs_broad_phase_layer_filter_interface;
  ObjectLayerPairFilterImpl object_layer_pair_filter_interface;

  auto init(this Physics& self) -> std::expected<void, std::string>;
  auto deinit(this Physics& self) -> std::expected<void, std::string>;

  auto new_system(this const Physics& self) -> std::unique_ptr<JPH::PhysicsSystem>;
  auto new_debug_renderer(this const Physics& self, DebugDrawList& target_draw_list)
    -> std::unique_ptr<PhysicsDebugRenderer>;

  auto get_temp_allocator(this const Physics& self) -> JPH::TempAllocatorImpl* { return self.temp_allocator.get(); }
  auto get_job_system(this const Physics& self) -> JPH::JobSystemWithBarrier* { return self.job_system.get(); }

private:
  std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator = nullptr;
  std::unique_ptr<JPH::JobSystemWithBarrier> job_system = nullptr;
};
} // namespace ox
