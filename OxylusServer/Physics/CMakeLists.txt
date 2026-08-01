# Jolt integration: the physics system, listeners, materials and ray casts.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusServer/Physics -B build/standalone-physics
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusServerPhysicsObjects LANGUAGES C CXX)
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

add_library(OxylusServerPhysicsObjects OBJECT ${OX_SOURCES})
add_library(ox::OxylusServerPhysicsObjects ALIAS OxylusServerPhysicsObjects)
ox_configure_module(OxylusServerPhysicsObjects)

ox_require_glm()
ox_require_jolt()
ox_require_flecs()
ox_require_sol2()
ox_require_simdjson()
ox_require_unordered_dense()

target_link_libraries(OxylusServerPhysicsObjects
  PUBLIC
    ox_server_headers
    glm::glm-header-only
    Jolt::Jolt
    flecs::flecs
    ox_sol2
    simdjson::simdjson
    unordered_dense::unordered_dense)
