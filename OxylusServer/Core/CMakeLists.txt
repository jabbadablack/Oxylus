# Module registry, event system, job manager, VFS, UUIDs and the shared scalar types.
#
# src/server.cpp is the headless executable's entry point. It lives with the module it belongs
# to but is excluded from the library and built by OxylusServer/CMakeLists.txt.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusServer/Core -B build/standalone-core
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusServerCore LANGUAGES C CXX)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
  include(OxBuild)
  include(OxHelpers)
  include(Dependencies)

  # Every module logs, and loguru's implementation is compiled into this one.
  add_subdirectory(
    "${OX_REPO_ROOT}/OxylusServer/Utils"
    "${CMAKE_BINARY_DIR}/OxylusServerUtils")
endif()

# server.cpp is the executable entry point, not part of the library.
ox_module_sources(OX_SOURCES EXCLUDE "/src/server\\.cpp$")

add_library(OxylusServerCore SHARED ${OX_SOURCES})
add_library(ox::OxylusServerCore ALIAS OxylusServerCore)
ox_configure_module(OxylusServerCore)

ox_require_unordered_dense()
ox_require_jolt()
ox_require_flecs()
ox_require_glm()
ox_require_sol2()
ox_require_zpp_bits()
ox_require_simdjson()
ox_require_svector()

target_link_libraries(OxylusServerCore
  PUBLIC
    ox_server_headers
    unordered_dense::unordered_dense
    Jolt::Jolt
    flecs::flecs
    glm::glm-header-only
    ox_sol2
    ox_zpp_bits
    simdjson::simdjson
    svector
  PRIVATE
    OxylusServerMemory
    OxylusServerOS)
