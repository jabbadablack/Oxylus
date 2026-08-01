#include "Scripting/LuaClientBindings.hpp"

#include "Scripting/LuaManager.hpp"
#include "Sim/SimHost.hpp"

#ifdef OX_LUA_BINDINGS
  #include "Scripting/LuaApplicationBindings.hpp" // IWYU pragma: export
  #include "Scripting/LuaDebugBindings.hpp"       // IWYU pragma: export
  #include "Scripting/LuaInputBindings.hpp"       // IWYU pragma: export
  #include "Scripting/LuaRMLBindings.hpp"         // IWYU pragma: export
  #include "Scripting/LuaRendererBindings.hpp"    // IWYU pragma: export
  #include "Scripting/LuaUIBindings.hpp"          // IWYU pragma: export
#endif

namespace ox {
auto bind_client_lua_bindings() -> void {
  ZoneScoped;

  if (!SimHost::has_mod<LuaManager>()) {
    return;
  }

#ifdef OX_LUA_BINDINGS
  auto& lua_manager = SimHost::mod<LuaManager>();
  auto* state = lua_manager.get_state();
  if (state == nullptr) {
    return;
  }

  #define BIND_CLIENT(type) lua_manager.bind<type>(#type, state)

  BIND_CLIENT(AppBinding);
  BIND_CLIENT(DebugBinding);
  BIND_CLIENT(InputBinding);
  BIND_CLIENT(RendererBinding);
  BIND_CLIENT(UIBinding);
  BIND_CLIENT(RMLBinding);

  #undef BIND_CLIENT
#endif
}
} // namespace ox
