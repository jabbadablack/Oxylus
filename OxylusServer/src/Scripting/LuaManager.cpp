#include "Scripting/LuaManager.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <sol/sol.hpp>

#include "Core/VFS.hpp"
#include "OS/File.hpp"
#include "Scripting/LuaNetworkBindings.hpp"
#include "Server/Server.hpp"

#ifdef OX_LUA_BINDINGS
  #include "Scripting/LuaAssetManagerBindings.hpp" // IWYU pragma: export
  #include "Scripting/LuaAudioBindings.hpp"        // IWYU pragma: export
  #include "Scripting/LuaFlecsBindings.hpp"        // IWYU pragma: export
  #include "Scripting/LuaMathBindings.hpp"         // IWYU pragma: export
  #include "Scripting/LuaPhysicsBindings.hpp"      // IWYU pragma: export
  #include "Scripting/LuaSceneBindings.hpp"        // IWYU pragma: export
  #include "Scripting/LuaVFSBindings.hpp"          // IWYU pragma: export
#endif

namespace ox {
auto LuaManager::init(this LuaManager& self) -> std::expected<void, std::string> {
  ZoneScoped;
  self.state = std::make_unique<sol::state>();
  self.state->open_libraries(
    sol::lib::base,
    sol::lib::package,
    sol::lib::math,
    sol::lib::table,
    sol::lib::os,
    sol::lib::string
  );

  self.state->set_function(
    "require_script",
    [s = self.state.get()](const std::string& virtual_dir, const std::string& path) -> sol::object {
      ZoneScopedN("LuaRequire");
      auto& vfs = Server::get_vfs();
      auto physical_path = vfs.resolve_physical_dir(virtual_dir, path);
      auto script = File::to_string(physical_path);
      return s->require_script(path, script);
    }
  );

#define BIND(type) self.bind<type>(#type, self.state.get())

#ifdef OX_LUA_BINDINGS
  self.bind_log();
  self.bind_vector();
  // Simulation bindings only. The presentation ones - App modules, Input, Debug draw, Renderer,
  // RmlUi, UI - are registered by bind_client_lua_bindings() once the client is up, because they
  // name types that do not exist on this side of the boundary.
  BIND(AssetManagerBinding);
  BIND(AudioBinding);
  BIND(FlecsBinding);
  BIND(MathBinding);
  BIND(PhysicsBinding);
  BIND(SceneBinding);
  BIND(VFSBinding);
  BIND(NetworkBinding);
#endif

  return {};
}

auto LuaManager::deinit(this LuaManager& self) -> std::expected<void, std::string> {
  self.state->collect_gc();
  self.state.reset();

  return {};
}

#define SET_LOG_FUNCTIONS(table, name, log_func)                                                                       \
  table.set_function(                                                                                                  \
    name,                                                                                                              \
    sol::overload(                                                                                                     \
      [](const std::string_view message) { log_func("{}", message); },                                                 \
      [](const glm::vec4& vec4) { log_func("x: {} y: {} z: {} w: {}", vec4.x, vec4.y, vec4.z, vec4.w); },              \
      [](const glm::vec3& vec3) { log_func("x: {} y: {} z: {}", vec3.x, vec3.y, vec3.z); },                            \
      [](const glm::vec2& vec2) { log_func("x: {} y: {}", vec2.x, vec2.y); },                                          \
      [](const glm::uvec2& vec2) { log_func("x: {} y: {}", vec2.x, vec2.y); }                                          \
    )                                                                                                                  \
  );

auto LuaManager::bind_log(this const LuaManager& self) -> void {
  ZoneScoped;
  sol::table log = self.state->create_named_table("Oxlog");

  SET_LOG_FUNCTIONS(log, "info", OX_LOG_INFO)
  SET_LOG_FUNCTIONS(log, "warn", OX_LOG_WARN)
  SET_LOG_FUNCTIONS(log, "error", OX_LOG_ERROR)
}

auto LuaManager::bind_vector(this const LuaManager& self) -> void {
  ZoneScoped;

  self.state->set_function("new_number_vector", []() { return std::vector<f64>{}; });
  self.state->set_function("new_string_vector", []() { return std::vector<std::string>{}; });
}
} // namespace ox
