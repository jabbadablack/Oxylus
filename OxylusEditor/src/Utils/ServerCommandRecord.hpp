#pragma once

#include <string>
#include <utility>

#include "Scene/Scene.hpp"
#include "Server/ServerCommand.hpp"
#include "Utils/Command.hpp"

namespace ox {
// An undoable edit expressed as a pair of simulation commands.
//
// This replaces the command classes that held raw pointers into ECS storage - a `T*` into a
// component, or a `flecs::entity` captured by value. Those could not survive a scene reload, a play
// mode copy, or the entity being destroyed and recreated, and they meant the editor was reaching
// into simulation memory to undo itself.
//
// Both directions are just commands, so undo is not a special path: it is the same apply point the
// forward edit went through.
class ServerCommandRecord : public Command {
public:
  ServerCommandRecord(Scene* scene, ServerCommand forward, ServerCommand inverse, std::string id, std::string merge_key = {})
      : scene_(scene),
        forward_(std::move(forward)),
        inverse_(std::move(inverse)),
        id_(std::move(id)),
        merge_key_(std::move(merge_key)) {}

  auto execute() -> void override {
    if (scene_ != nullptr) {
      static_cast<void>(apply_command(*scene_, forward_));
    }
  }

  auto undo() -> void override {
    if (scene_ != nullptr) {
      static_cast<void>(apply_command(*scene_, inverse_));
    }
  }

  auto get_id() const -> std::string_view override { return id_; }

  // A drag issues one command per frame. Merging collapses them into a single undo step whose
  // forward is the latest position and whose inverse is still the one from before the drag started.
  auto can_merge(const Command& other) const -> bool override {
    const auto* record = dynamic_cast<const ServerCommandRecord*>(&other);
    return record != nullptr && !merge_key_.empty() && record->merge_key_ == merge_key_;
  }

  auto merge(std::unique_ptr<Command> other) -> std::unique_ptr<Command> override {
    auto* record = dynamic_cast<ServerCommandRecord*>(other.get());
    if (record == nullptr) {
      return nullptr;
    }

    // Keep this record's inverse - the older state - and take the newer forward.
    return std::make_unique<ServerCommandRecord>(scene_, record->forward_, inverse_, id_, merge_key_);
  }

private:
  Scene* scene_ = nullptr;
  ServerCommand forward_ = {};
  ServerCommand inverse_ = {};
  std::string id_ = {};
  std::string merge_key_ = {};
};
} // namespace ox
