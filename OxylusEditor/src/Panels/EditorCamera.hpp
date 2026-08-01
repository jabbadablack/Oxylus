#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "Core/Types.hpp"
#include "Scene/Components.hpp"
#include "Server/FrameSnapshot.hpp"

namespace ox {
// The editor's fly-cam, owned by the viewport that draws through it.
//
// This used to be an entity injected into the scene's world, which meant a purely presentational
// concern was ECS state: it got serialised into saved scenes, replicated by any future snapshot,
// and written to every frame by the editor. Now it is plain client state, and the viewport tells
// the simulation where it is looking via a ViewRequest.
//
// CameraComponent is reused as the storage so Camera::update still computes the matrices, and so
// the gizmo and picking code keeps working against the same type.
struct EditorCamera {
  CameraComponent camera = {};
  TransformComponent transform = {};

  // Smooth-damp state, previously kept on the panel.
  glm::vec3 translation_velocity = {};
  glm::vec2 rotation_velocity = {};
  f32 translation_dampening = 0.f;
  f32 rotation_dampening = 0.f;

  // Runs the fly-cam for one frame and recomputes the matrices for `viewport_extent`. `panning`
  // carries the locked mouse position the panel tracks for middle-drag panning.
  auto update(
    this EditorCamera& self, f32 delta_time, glm::uvec2 viewport_extent, const glm::vec2& locked_mouse_position
  ) -> void;

  // Keeps the matrices in step with a resized viewport even on frames where nothing moved.
  auto refresh(this EditorCamera& self, glm::uvec2 viewport_extent) -> void;

  auto to_camera_data(this const EditorCamera& self) -> GPU::CameraData;
};
} // namespace ox
