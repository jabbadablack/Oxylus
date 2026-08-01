# Colour helpers, ImGui RAII scopes, TOML and JSON helpers, the profiler shim. The only client
# module that calls into no other.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusClient/Utils -B build/standalone-utils
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusClientUtils LANGUAGES C CXX)
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

add_library(OxylusClientUtils STATIC ${OX_SOURCES})
add_library(ox::OxylusClientUtils ALIAS OxylusClientUtils)
ox_configure_module(OxylusClientUtils)

ox_require_glm()
ox_require_simdjson()
ox_require_tomlplusplus()
ox_require_vuk()
ox_require_imgui()
ox_require_zpp_bits()
ox_require_unordered_dense()
ox_require_vk_bootstrap()

target_link_libraries(OxylusClientUtils
  PUBLIC
    ox_client_headers
    glm::glm-header-only
    simdjson::simdjson
    tomlplusplus::tomlplusplus
    vuk
    ox_imgui
    ox_zpp_bits
    unordered_dense::unordered_dense
    vk-bootstrap::vk-bootstrap)
