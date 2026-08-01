#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "Core/Option.hpp"
#include "Core/Types.hpp"
#include "Core/UUID.hpp"
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

// Identity edits. None of these touch a component, which is why they need their own commands - a
// component blob would carry nothing.
struct CmdRenameEntity {
  EntityHandle entity = EntityHandle::Invalid;
  std::string name = {};
};

struct CmdSetEntityEnabled {
  EntityHandle entity = EntityHandle::Invalid;
  bool enabled = true;
};

// parent == Invalid unparents.
struct CmdReparentEntity {
  EntityHandle entity = EntityHandle::Invalid;
  EntityHandle parent = EntityHandle::Invalid;
};

struct CmdCloneEntity {
  EntityHandle entity = EntityHandle::Invalid;
};

// The archetype names what the editor's create menu offers - "entity", "sprite", "camera",
// "light", "sun", "audio_source". The server decides what those mean, so the client is not
// authoring worlds by the back door.
struct CmdCreateEntity {
  std::string name = {};
  std::string archetype = {};
};

struct CmdSpawnModel {
  UUID model_uuid = UUID(nullptr);
};

// The server serialises, which also removes the old race where a job thread walked the world while
// it was being ticked.
struct CmdSaveScene {
  std::string path = {};
};

using ServerCommandPayload = std::variant<
  CmdDestroyEntity,
  CmdRestoreEntity,
  CmdSetComponent,
  CmdSetTransform,
  CmdRenameEntity,
  CmdSetEntityEnabled,
  CmdReparentEntity,
  CmdCloneEntity,
  CmdCreateEntity,
  CmdSpawnModel,
  CmdSaveScene>;

struct ServerCommand {
  ServerCommandPayload payload = {};
};

// Wire format for a command. Hand-rolled rather than run through zpp_bits: the transform payload
// is glm types, whose anonymous unions are exactly the shape a reflection-based serializer gets
// wrong, and a command is small enough that an explicit codec is cheaper than finding out.
auto serialize_command(const ServerCommand& command) -> std::vector<u8>;
auto deserialize_command(std::span<const u8> bytes) -> option<ServerCommand>;

// Applies one command to the world. The single place anything outside a system mutates simulation
// state. Returns the entity acted on where that is meaningful.
auto apply_command(Scene& scene, const ServerCommand& command) -> EntityHandle;
} // namespace ox
