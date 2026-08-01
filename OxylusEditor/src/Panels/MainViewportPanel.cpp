#include "MainViewportPanel.hpp"

#include <algorithm>
#include <icons/IconsMaterialDesignIcons.h>
#include <vuk/ImageAttachment.hpp>

#include "Core/App.hpp"
#include "Editor.hpp"
#include "Networking/NetPacket.hpp"
#include "Server/Server.hpp"
#include "UI/PayloadData.hpp"
#include "UI/UI.hpp"

namespace ox {
MainViewportPanel::MainViewportPanel() : EditorPanelState("Scenes", ICON_MDI_VIDEO_3D, true, false) {}

auto MainViewportPanel::init(this MainViewportPanel& self) -> void {
  auto& event_system = App::get_event_system();
  self.app_close_handler = event_system
                             .subscribe<AppCloseEvent>([&self](const AppCloseEvent& e) {
                               for (auto& v : self.viewport_panels) {
                                 if (v->get_scene()->is_playing()) {
                                   v->get_scene()->stop();
                                 }
                               }
                             })
                             .value_or(0);
  self.scene_load_handler = event_system
                              .subscribe<Editor::ViewportSceneLoadEvent>(
                                [&self](const Editor::ViewportSceneLoadEvent& e) { self.update_dockspace(); }
                              )
                              .value_or(0);
  self.scene_play_handler = event_system
                              .subscribe<Editor::ScenePlayEvent>([&self](const Editor::ScenePlayEvent& e) {
                                // The server makes and runs the copy. It arrives as a new
                                // scene id through replication, so there is nothing to build
                                // locally - doing so was an orphan world that never ticked.
                                auto& editor = App::mod<Editor>();
                                const auto server_id = editor.server_scene_id(e.scene_id);
                                App::send_rpc(
                                  "scene.play",
                                  std::array{RPCParameter{.value = static_cast<i64>(server_id)}}
                                );
                              })
                              .value_or(0);
  self.scene_stop_handler =
    event_system
      .subscribe<Editor::SceneStopEvent>([&self](const Editor::SceneStopEvent& e) {
        // The play scene lives on the server; stopping is its call. Guarded on is_playing() - a
        // SceneStopEvent is raised for scenes that were never playing, and stopping one of those
        // destroyed the authored world.
        auto& editor = App::mod<Editor>();
        const auto stopping = editor.scene_manager.try_get_scene(e.scene_id);
        if (stopping && stopping->is_playing()) {
          if (const auto server_id = editor.server_scene_id(e.scene_id); server_id != ~0_u64) {
            App::send_rpc(proc::SCENE_STOP, std::array{RPCParameter{.value = static_cast<i64>(server_id)}});

            // Same in-flight problem closing a scene had: snapshots for this copy are already on
            // the wire, and the first one back looked like a new scene and reopened the tab - which
            // is why stopping only took on the second click.
            editor.closed_scenes.emplace(server_id);
            editor.replica_scenes.erase(server_id);
          }
        }

        auto should_stop_and_remove = [e, &self](const std::unique_ptr<ViewportPanel>& panel) {
          if (!panel) {
            return true;
          }

          auto* editor_scene = panel->get_scene();

          if (editor_scene && editor_scene->get_id() == e.scene_id && editor_scene->is_playing()) {
            editor_scene->stop();
            auto& editor = App::mod<Editor>();
            editor.scene_manager.remove_scene(e.scene_id);
            self.update_dockspace();
            return true;
          }

          return false;
        };

        // We need this since we can't erase while iterating in on_render
        App::defer_to_next_frame([&self, should_stop_and_remove] {
          std::erase_if(self.viewport_panels, should_stop_and_remove);
          std::erase_if(self.pending_viewports, should_stop_and_remove);
        });
      })
      .value_or(0);
}

auto MainViewportPanel::deinit(this MainViewportPanel& self) -> void {
  ZoneScoped;

  // Every handler above captures `self`, and the event system outlives this panel.
  auto& event_system = App::get_event_system();
  std::ignore = event_system.unsubscribe<AppCloseEvent>(self.app_close_handler);
  std::ignore = event_system.unsubscribe<Editor::ViewportSceneLoadEvent>(self.scene_load_handler);
  std::ignore = event_system.unsubscribe<Editor::ScenePlayEvent>(self.scene_play_handler);
  std::ignore = event_system.unsubscribe<Editor::SceneStopEvent>(self.scene_stop_handler);
}

auto MainViewportPanel::reset(this MainViewportPanel& self) -> void {
  ZoneScoped;

  self.viewport_panels.clear();
  self.pending_viewports.clear();
}

auto MainViewportPanel::get_focused_viewport(this const MainViewportPanel& self) -> ViewportPanel* {
  ZoneScoped;

  for (auto& viewport : self.viewport_panels) {
    if (viewport->is_viewport_focused) {
      return viewport.get();
    }
  }

  return nullptr;
}

auto MainViewportPanel::get_visible_viwports(this const MainViewportPanel& self) -> std::vector<ViewportPanel*> {
  ZoneScoped;

  auto v = std::vector<ViewportPanel*>{};

  for (auto& viewport : self.viewport_panels) {
    if (viewport->visible) {
      v.emplace_back(viewport.get());
    }
  }

  return v;
}

auto MainViewportPanel::is_any_scene_playing(this const MainViewportPanel& self) -> bool {
  ZoneScoped;

  const auto playing = [](const std::unique_ptr<ViewportPanel>& panel) {
    const auto* scene = panel ? panel->get_scene() : nullptr;
    return scene != nullptr && scene->is_playing();
  };

  return std::ranges::any_of(self.viewport_panels, playing) || std::ranges::any_of(self.pending_viewports, playing);
}

auto MainViewportPanel::is_fullscreen(this const MainViewportPanel& self) -> bool {
  ZoneScoped;

  return self.fullscreen_viewport;
}

auto MainViewportPanel::toggle_fullscreen(this MainViewportPanel& self) -> void {
  ZoneScoped;

  self.fullscreen_viewport = !self.fullscreen_viewport;
}

auto MainViewportPanel::add_new_scene(this MainViewportPanel& self, const std::shared_ptr<EditorScene>& scene) -> void {
  ZoneScoped;

  auto* viewport = self.add_viewport();
  viewport->set_context(scene);

  self.update_dockspace();
}

auto MainViewportPanel::add_new_play_scene(this MainViewportPanel& self, const std::shared_ptr<EditorScene>& scene)
  -> void {
  auto* viewport = self.add_viewport();
  viewport->set_context(scene);
  viewport->set_icon(ICON_MDI_CONTROLLER);
  viewport->set_name(fmt::format("Game:{}", scene->get_scene()->scene_name));

  self.update_dockspace();
}

auto MainViewportPanel::add_viewport(this MainViewportPanel& self) -> ViewportPanel* {
  // We create the viewport in the pending queue,
  // this prevents on_render() from seeing it until on_update() has flushed the queue
  auto& viewport = self.pending_viewports.emplace_back(std::make_unique<ViewportPanel>());

  self.update_dockspace();

  return viewport.get();
}

void MainViewportPanel::on_render(this MainViewportPanel& self, vuk::ImageAttachment swapchain_attachment) {
  if (self.on_begin(ImGuiWindowFlags_MenuBar)) {
    auto viewport_size = ImGui::GetContentRegionAvail();
    auto& style = ImGui::GetStyle();
    if (ImGui::BeginMenuBar()) {
      if (ImGui::MenuItem(ICON_MDI_PLUS_THICK)) {
        App::mod<Editor>().new_scene();
      }
      UI::tooltip_hover("New scene");
      if (ImGui::MenuItem(ICON_MDI_FOLDER_OPEN)) {
        App::mod<Editor>().open_scene_file_dialog();
      }
      UI::tooltip_hover("Open scene");
      auto button_width = ImGui::CalcTextSize(ICON_MDI_ARROW_EXPAND_ALL, nullptr, true);
      ImGui::SetCursorPosX(viewport_size.x - button_width.x - (style.ItemInnerSpacing.x * 2.f));
      if (ImGui::MenuItem(ICON_MDI_ARROW_EXPAND_ALL)) {
        self.fullscreen_viewport = !self.fullscreen_viewport;
      }
      ImGui::EndMenuBar();
    }

    auto dockspace_id = ImGui::GetID("ViewportDockspace");

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    self.drag_drop();

    if (self.dock_should_update) {
      self.set_dockspace();
      self.dock_should_update = false;
    }

    for (const auto& panel : self.viewport_panels) {
      panel->on_render(swapchain_attachment);
    }
  }

  self.on_end();
}

void MainViewportPanel::update(this MainViewportPanel& self, const Timestep& timestep, SceneHierarchyPanel* sh) {
  ZoneScoped;

  // Move pending viewports to the main list
  if (!self.pending_viewports.empty()) {
    self.viewport_panels.insert(
      self.viewport_panels.end(),
      std::make_move_iterator(self.pending_viewports.begin()),
      std::make_move_iterator(self.pending_viewports.end())
    );
    self.pending_viewports.clear();
    self.update_dockspace();
  }

  for (const auto& panel : self.viewport_panels) {
    auto* panel_scene = panel->get_scene();

    if (panel->is_viewport_focused) {
      auto sh_scene = sh->get_scene();

      if (sh_scene != panel_scene) {
        if (!sh_scene || !panel_scene || sh_scene->get_id() != panel_scene->get_id()) {
          sh->set_scene(panel_scene);
        }
      }
    }

    // Only the view request. Gating the gameplay phases used to happen here too, on the replica -
    // which never ticks, so it decided nothing. Play state lives on the server now.
    if (panel_scene) {
      panel->publish_view_request();
    }
  }

  // Two of a scene's renderer cvars are read by SERVER-side systems - the bounding-box and physics
  // debug draws run where the world is simulated. The editor toggles them on its replica, which
  // nothing reads, so the value has to be sent. Pushed on change rather than every frame.
  for (const auto& panel : self.viewport_panels) {
    auto* panel_scene = panel->get_scene();
    if (!panel_scene) {
      continue;
    }

    auto& cvars = panel_scene->get_scene()->renderer_cvar;
    const auto push = [](const std::string_view name, const i32 value) {
      App::send_rpc(
        proc::CVAR_SET,
        std::array{RPCParameter{.value = std::string(name)}, RPCParameter{.value = static_cast<f32>(value)}}
      );
    };

    const auto boxes = cvars.cvar_draw_bounding_boxes.get();
    if (boxes != self.last_sent_draw_bounding_boxes) {
      self.last_sent_draw_bounding_boxes = boxes;
      push("rr.draw_bounding_boxes", boxes);
    }

    const auto physics = cvars.cvar_enable_physics_debug_renderer.get();
    if (physics != self.last_sent_physics_debug) {
      self.last_sent_physics_debug = physics;
      push("rr.enable_physics_debug_renderer", physics);
    }
  }

  // No simulation tick here, and none anywhere else in the editor - the world is simulated by the
  // OxylusServer process and arrives as state. The extract still runs locally though: turning world
  // state into render data is the client's job, and without it frame_snapshot stays empty and the
  // viewport renders black while gizmos and icons (drawn separately) keep working.
  for (const auto& panel : self.viewport_panels) {
    auto* panel_scene = panel->get_scene();

    if (panel_scene) {
      auto scene = panel_scene->get_scene();
      scene->extract_for_render();
      panel->render_scene.prepare(scene->frame_snapshot, scene->renderer_cvar);
    }

    if (panel->visible) {
      panel->on_update();
    }
  }

  // A closed tab is a closed scene. Dropping only the local replica left the server still
  // replicating it, and the next snapshot simply reopened the tab.
  std::erase_if(self.viewport_panels, [](const std::unique_ptr<ViewportPanel>& ptr) {
    if (ptr != nullptr && ptr->visible) {
      return false;
    }

    if (ptr != nullptr) {
      if (auto* editor_scene = ptr->get_scene()) {
        App::mod<Editor>().close_replica_scene(editor_scene->get_id());
      }
    }

    return true;
  });
}

auto MainViewportPanel::update_dockspace(this MainViewportPanel& self) -> void {
  ZoneScoped;
  self.dock_should_update = true;
}

auto MainViewportPanel::set_dockspace(this const MainViewportPanel& self) -> void {
  auto dock_id = ImGui::GetID("ViewportDockspace");

  for (auto& panel : self.viewport_panels) {
    ImGui::DockBuilderDockWindow(panel->get_id(), dock_id);
  }

  ImGui::DockBuilderFinish(dock_id);
}

void MainViewportPanel::drag_drop(this MainViewportPanel& self) {
  auto& editor = App::mod<Editor>();

  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* imgui_payload = ImGui::AcceptDragDropPayload(PayloadData::DRAG_DROP_SOURCE)) {
      const auto* payload = PayloadData::from_payload(imgui_payload);
      const auto path = payload->get_path();
      if (path.extension() == ".oxscene") {
        auto scene_id = editor.scene_manager.load_scene(path);
        if (scene_id.has_value()) {
          self.add_new_scene(App::mod<Editor>().scene_manager.get_scene(scene_id.value()));
        }
      }
    }

    ImGui::EndDragDropTarget();
  }
}

} // namespace ox
