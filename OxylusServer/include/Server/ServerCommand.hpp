#pragma once

#include <span>
#include <string>
#include <variant>

#include "Core/Types.hpp"
#include "Scene/SceneSnapshot.hpp"
#include "Server/Fwd.hpp"

namespace ox {
class Scene;

// A structural edit to the world, addressed to a specific entity and carrying its own payload.
//
// Deliberately small: these exist so the editor can undo without holding raw pointers into ECS
// storage, and every type here has a call site. Tunable settings are not commands - those are
// CVars (RendererCVar, ContextCVar), which already do that job. Add a type when something needs it.
struct CmdDestroyEntity {
  EntityHandle entity = EntityHandle::Invalid;
};

// Rehydrates a subtree from the JSON the editor captured before destroying it. Note the restored
// entity gets a fresh id, so commands recorded against the old one do not survive an undo of a
// delete.
struct CmdRestoreEntity {
  EntityHandle parent = EntityHandle::Invalid;
  std::string serialized_json = {};
};

// Carries the reflected encoding from ComponentBlob, never a raw byte copy: several components own
// heap storage or hold raw engine pointers.
struct CmdSetComponent {
  EntityHandle entity = EntityHandle::Invalid;
  ComponentState state = {};
};

// Separate from CmdSetComponent because transforms are the common edit - a gizmo drag issues one per
// frame - and this avoids encoding a blob for each.
struct CmdSetTransform {
  EntityHandle entity = EntityHandle::Invalid;
  glm::vec3 position = {};
  glm::quat rotation = {};
  glm::vec3 scale = glm::vec3(1.f);
};

using ServerCommandPayload = std::variant<CmdDestroyEntity, CmdRestoreEntity, CmdSetComponent, CmdSetTransform>;

struct ServerCommand {
  ServerCommandPayload payload = {};
};

// Applies one command to the world. The single place anything outside a system mutates simulation
// state. Returns the entity acted on where that is meaningful.
auto apply_command(Scene& scene, const ServerCommand& command) -> EntityHandle;
} // namespace ox
