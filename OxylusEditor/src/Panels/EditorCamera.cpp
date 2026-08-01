#include "EditorCamera.hpp"

#include <imgui.h>

#include "Core/App.hpp"
#include "Core/Input.hpp"
#include "Editor.hpp"
#include "Render/Camera.hpp"
#include "Render/Window.hpp"
#include "Utils/OxMath.hpp"

namespace ox {
auto EditorCamera::refresh(this EditorCamera& self, const glm::uvec2 viewport_extent) -> void {
  ZoneScoped;

  self.camera.position = self.transform.position;
  Camera::update(self.camera, glm::vec2(viewport_extent));
}

auto EditorCamera::update(
  this EditorCamera& self,
  const f32 delta_time,
  const glm::uvec2 viewport_extent,
  const glm::vec2& locked_mouse_position
) -> void {
  ZoneScoped;

  auto& editor = App::mod<Editor>();

  auto& cam = self.camera;
  auto& tc = self.transform;

  const glm::vec3& position = cam.position;
  const glm::vec2 yaw_pitch = glm::vec2(cam.yaw, cam.pitch);
  glm::vec3 final_position = position;
  glm::vec2 final_yaw_pitch = yaw_pitch;

  const auto is_ortho = cam.projection == CameraComponent::Projection::Orthographic;
  if (is_ortho) {
    final_position = {0.0f, 0.0f, 0.0f};
    final_yaw_pitch = {0.f, 0.f};
  }

  const auto& window = App::get_window();

  auto& input_sys = App::mod<Input>();
  if (input_sys.get_key_pressed(ScanCode::F)) {
    auto& editor_context = editor.get_context();
    if (editor_context.entity.has_value()) {
      const auto entity_tc = editor_context.entity->get<TransformComponent>();
      auto final_pos = entity_tc.position + cam.forward;
      final_pos += -5.0f * cam.forward * glm::vec3(1.0f);
      cam.position = final_pos;
    }
  }

  const auto actual_sens = editor.editor_cvar.cvar_camera_sens.get() / 10.f;
  const auto smoothed_sens = actual_sens * 100.f;
  const auto camera_sens = editor.editor_cvar.cvar_camera_smooth.get() ? smoothed_sens : actual_sens;

  const auto actual_speed = editor.editor_cvar.cvar_camera_speed.get();
  const auto smoothed_speed = actual_speed * 100.f;
  const auto camera_speed = editor.editor_cvar.cvar_camera_smooth.get() ? smoothed_speed : actual_speed;

  if ((input_sys.get_mouse_held(MouseCode::Middle) || input_sys.get_mouse_held(MouseCode::Right)) && !is_ortho) {
    const glm::vec2 new_mouse_position = input_sys.get_mouse_position_rel();
    window.set_cursor_override(WindowCursor::Crosshair);

    if (input_sys.get_mouse_moved()) {
      const glm::vec2 change = new_mouse_position * camera_sens;
      final_yaw_pitch.x += change.x;
      final_yaw_pitch.y = glm::clamp(final_yaw_pitch.y - change.y, glm::radians(-89.9f), glm::radians(89.9f));
    }

    const float max_move_speed = camera_speed * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f : 1.0f) * delta_time;

    if (input_sys.get_key_held(ScanCode::W))
      final_position += cam.forward * max_move_speed;
    else if (input_sys.get_key_held(ScanCode::S))
      final_position -= cam.forward * max_move_speed;
    if (input_sys.get_key_held(ScanCode::D))
      final_position += cam.right * max_move_speed;
    else if (input_sys.get_key_held(ScanCode::A))
      final_position -= cam.right * max_move_speed;

    if (input_sys.get_key_held(ScanCode::Q)) {
      final_position.y -= max_move_speed;
    } else if (input_sys.get_key_held(ScanCode::E)) {
      final_position.y += max_move_speed;
    }
  }
  // Panning
  else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
    const glm::vec2 new_mouse_position = input_sys.get_mouse_position_rel();
    window.set_cursor_override(WindowCursor::ResizeAll);

    const glm::vec2 change = (new_mouse_position - locked_mouse_position) * 1.f;

    if (input_sys.get_mouse_moved()) {
      const float max_move_speed = camera_speed * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f : 1.0f) * delta_time;
      final_position += cam.forward * change.y * max_move_speed;
      final_position += cam.right * change.x * max_move_speed;
    }
  }

  const glm::vec3 damped_position = math::smooth_damp(
    position,
    final_position,
    self.translation_velocity,
    self.translation_dampening,
    1000.0f,
    delta_time
  );
  const glm::vec2 damped_yaw_pitch =
    math::smooth_damp(yaw_pitch, final_yaw_pitch, self.rotation_velocity, self.rotation_dampening, 1000.0f, delta_time);

  const auto smooth = editor.editor_cvar.cvar_camera_smooth.as_bool();
  tc.position = smooth ? damped_position : final_position;
  const float applied_pitch = smooth ? damped_yaw_pitch.y : final_yaw_pitch.y;
  const float applied_yaw = smooth ? damped_yaw_pitch.x : final_yaw_pitch.x;
  tc.rotation = glm::quat(glm::vec3(applied_pitch, applied_yaw, 0.0f));
  cam.pitch = applied_pitch;
  cam.yaw = applied_yaw;
  cam.zoom = static_cast<float>(editor.editor_cvar.cvar_camera_zoom.get());

  self.refresh(viewport_extent);
}

auto EditorCamera::to_camera_data(this const EditorCamera& self) -> GPU::CameraData {
  ZoneScoped;

  auto data = GPU::CameraData{
    .position = glm::vec4(self.camera.position, 0.0f),
    .projection = self.camera.get_projection_matrix(),
    .inv_projection = self.camera.get_inv_projection_matrix(),
    .view = self.camera.get_view_matrix(),
    .inv_view = self.camera.get_inv_view_matrix(),
    .projection_view = self.camera.get_projection_matrix() * self.camera.get_view_matrix(),
    .inv_projection_view = self.camera.get_inverse_projection_view(),
    .temporalaa_jitter = self.camera.jitter,
    .near_clip = self.camera.near_clip,
    .far_clip = self.camera.far_clip,
    .fov = self.camera.fov,
    .output_index = 0,
    .acceptable_lod_error = 2.0f,
  };

  math::calc_frustum_planes(data.projection_view, data.frustum_planes);

  return data;
}
} // namespace ox
