#pragma once

namespace ox {
// Registers the Lua bindings that name presentation types - App modules, Input, the debug renderer,
// the renderer, RmlUi and the UI helpers - into the simulation's LuaManager.
//
// LuaManager itself is simulation-side and cannot reference any of them, so the client hands them
// over once it is up. Called from App::init after the presentation modules have initialised.
auto bind_client_lua_bindings() -> void;
} // namespace ox
