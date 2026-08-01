# The render state the simulation owns: cameras, frusta, bounding volumes and the per-scene
# renderer CVars. No vuk, no GPU - that is the client's Render module.
#
# flecs is linked for its headers only: Camera.cpp includes Scene/Components.hpp, which names
# component types. This module references no flecs symbol, so no flecs object is pulled in and its
# global state stays in OxylusServerLib where the world actually lives.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusServer/Render -B build/standalone-render
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusServerRender LANGUAGES C CXX)
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

add_library(OxylusServerRender SHARED ${OX_SOURCES})
add_library(ox::OxylusServerRender ALIAS OxylusServerRender)
ox_configure_module(OxylusServerRender)

ox_require_glm()
ox_require_simdjson()
ox_require_flecs()
ox_require_unordered_dense()

target_link_libraries(OxylusServerRender
  PUBLIC
    ox_server_headers
    glm::glm-header-only
    simdjson::simdjson
    flecs::flecs
    unordered_dense::unordered_dense)
