# The vuk renderer: render context, per-scene instance, the pass library and the window.
# src/Shaders/ is compiled separately into .oxpack archives by ox_compile_shaders().

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusClient/Render -B build/standalone-render
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusClientRenderObjects LANGUAGES C CXX)
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

add_library(OxylusClientRenderObjects OBJECT ${OX_SOURCES})
add_library(ox::OxylusClientRenderObjects ALIAS OxylusClientRenderObjects)
ox_configure_module(OxylusClientRenderObjects)

ox_require_glm()
ox_require_unordered_dense()
ox_require_tomlplusplus()
ox_require_meshoptimizer()
ox_require_stb()
ox_require_zpp_bits()
ox_require_sdl3()
ox_require_vk_bootstrap()
ox_require_vuk()
ox_require_rmlui()
ox_require_jolt()
ox_require_flecs()
ox_require_imgui()
ox_require_sol2()
ox_require_simdjson()

target_link_libraries(OxylusClientRenderObjects
  PUBLIC
    ox_client_headers
    glm::glm-header-only
    unordered_dense::unordered_dense
    tomlplusplus::tomlplusplus
    meshoptimizer
    ox_stb
    ox_zpp_bits
    SDL3::SDL3-static
    vk-bootstrap::vk-bootstrap
    vuk
    plf_colony
    Vulkan::Headers
    RmlUi::RmlUi
    Jolt::Jolt
    flecs::flecs
    ox_imgui
    ox_sol2
    simdjson::simdjson)
