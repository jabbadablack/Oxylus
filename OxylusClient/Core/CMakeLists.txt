# App, the module registration order, input, projects.
#
# src/runtime.cpp is the game runtime's entry point - App plus DefaultModules with no Editor
# module. It is excluded from the library and built by OxylusClient/CMakeLists.txt.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusClient/Core -B build/standalone-core
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusClientCoreObjects LANGUAGES C CXX)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
  include(OxBuild)
  include(OxHelpers)
  include(Dependencies)

  # Every module logs, and loguru's implementation is compiled into this one.
  add_subdirectory(
    "${OX_REPO_ROOT}/OxylusServer/Utils"
    "${CMAKE_BINARY_DIR}/OxylusServerUtils")
endif()

# runtime.cpp is the executable entry point, not part of the library.
ox_module_sources(OX_SOURCES EXCLUDE "/src/runtime\\.cpp$")

add_library(OxylusClientCoreObjects OBJECT ${OX_SOURCES})
add_library(ox::OxylusClientCoreObjects ALIAS OxylusClientCoreObjects)
ox_configure_module(OxylusClientCoreObjects)

ox_require_glm()
ox_require_unordered_dense()
ox_require_tomlplusplus()
ox_require_sdl3()
ox_require_vuk()
ox_require_jolt()
ox_require_rmlui()
ox_require_flecs()
ox_require_imgui()
ox_require_sol2()
ox_require_zpp_bits()
ox_require_simdjson()
ox_require_svector()
ox_require_vk_bootstrap()

target_link_libraries(OxylusClientCoreObjects
  PUBLIC
    ox_client_headers
    glm::glm-header-only
    unordered_dense::unordered_dense
    tomlplusplus::tomlplusplus
    SDL3::SDL3-static
    vuk
    Jolt::Jolt
    RmlUi::RmlUi
    Vulkan::Headers
    flecs::flecs
    ox_imgui
    ox_sol2
    ox_zpp_bits
    simdjson::simdjson
    svector
    vk-bootstrap::vk-bootstrap)

# App.cpp spawns the server process by name, so the name lives in one place. This has to be defined
# on the module that compiles App.cpp, not on the aggregate that links it - a PRIVATE define on
# OxylusClientLib never reaches an object library's own compilation.
if(WIN32)
  target_compile_definitions(OxylusClientCoreObjects PRIVATE OX_SERVER_EXECUTABLE="OxylusServer.exe")
else()
  target_compile_definitions(OxylusClientCoreObjects PRIVATE OX_SERVER_EXECUTABLE="OxylusServer")
endif()
