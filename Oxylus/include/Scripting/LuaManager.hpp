#pragma once

#include <ankerl/unordered_dense.h>
#include <expected>
#include <sol/state.hpp>

#include "Scripting/LuaBinding.hpp"

namespace ox {
// Registers the Lua bindings that name presentation types - App modules, Input, the debug renderer,
// the renderer, RmlUi, UI - into the simulation's LuaManager. LuaManager cannot reference any of
// them, so the client hands them over from App::init once its modules exist.
auto bind_client_lua_bindings() -> void;

class LuaManager {
public:
  constexpr static auto MODULE_NAME = "LuaManager";

  // Simulation-side: App::with<T>() and App::mod<T>() route this to SimHost.
  constexpr static bool SIM_MODULE = true;

  auto init(this LuaManager& self) -> std::expected<void, std::string>;
  auto deinit(this LuaManager& self) -> std::expected<void, std::string>;

  auto get_state(this const LuaManager& self) -> sol::state* { return self.state.get(); }

  template <typename T>
  void bind(this LuaManager& self, const std::string& name, sol::state* state) {
    static_assert(std::is_base_of_v<LuaBinding, T>, "T must derive from LuaBinding");
    auto binding = std::make_unique<T>();
    binding->bind(state);
    self.bindings.emplace(name, std::move(binding));
  }

  template <typename T>
  auto get_binding(this LuaManager& self, const std::string& name) -> T* {
    static_assert(std::is_base_of_v<LuaBinding, T>, "T must derive from LuaBinding");
    return dynamic_cast<T*>(self.bindings[name].get());
  }

private:
  ankerl::unordered_dense::map<std::string, std::unique_ptr<LuaBinding>> bindings = {};
  std::unique_ptr<sol::state> state = nullptr;

  auto bind_log(this const LuaManager& self) -> void;
  auto bind_vector(this const LuaManager& self) -> void;
};
} // namespace ox
