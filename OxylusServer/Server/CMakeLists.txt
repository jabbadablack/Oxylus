# The simulation host: scene registration, the tick, frame snapshots and scene extraction.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusServer/Server -B build/standalone-server
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusServerServerObjects LANGUAGES C CXX)
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

add_library(OxylusServerServerObjects OBJECT ${OX_SOURCES})
add_library(ox::OxylusServerServerObjects ALIAS OxylusServerServerObjects)
ox_configure_module(OxylusServerServerObjects)

ox_require_glm()
ox_require_flecs()
ox_require_jolt()
ox_require_sol2()
ox_require_zpp_bits()
ox_require_simdjson()
ox_require_svector()
ox_require_unordered_dense()

target_link_libraries(OxylusServerServerObjects
  PUBLIC
    ox_server_headers
    glm::glm-header-only
    flecs::flecs
    Jolt::Jolt
    ox_sol2
    ox_zpp_bits
    simdjson::simdjson
    svector
    unordered_dense::unordered_dense)
