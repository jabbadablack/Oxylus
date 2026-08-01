# ImGui and RmlUi rendering plus the built-in viewers and the text editor.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusClient/UI -B build/standalone-ui
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusClientUIObjects LANGUAGES C CXX)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
  include(OxBuild)
  include(OxHelpers)
  include(Dependencies)

  # Every module logs, and loguru's implementation is compiled into this one.
  add_subdirectory(
    "${OX_REPO_ROOT}/OxylusServer/Utils"
    "${CMAKE_BINARY_DIR}/OxylusServerUtils")
endif()

ox_module_sources(OX_SOURCES)

add_library(OxylusClientUIObjects OBJECT ${OX_SOURCES})
add_library(ox::OxylusClientUIObjects ALIAS OxylusClientUIObjects)
ox_configure_module(OxylusClientUIObjects)

ox_require_glm()
ox_require_unordered_dense()
ox_require_imgui()
ox_require_sdl3()
ox_require_vuk()
ox_require_rmlui()
ox_require_jolt()
ox_require_flecs()
ox_require_sol2()
ox_require_zpp_bits()
ox_require_simdjson()
ox_require_svector()
ox_require_vk_bootstrap()

target_link_libraries(OxylusClientUIObjects
  PUBLIC
    ox_client_headers
    glm::glm-header-only
    unordered_dense::unordered_dense
    ox_imgui
    SDL3::SDL3-static
    vuk
    RmlUi::RmlUi
    RmlUi::Lua
    Jolt::Jolt
    Vulkan::Headers
    flecs::flecs
    ox_sol2
    ox_zpp_bits
    simdjson::simdjson
    svector
    vk-bootstrap::vk-bootstrap)

# ImGuiSPV_{VS,FS}.hpp sit next to the code that embeds them.
target_include_directories(OxylusClientUIObjects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
