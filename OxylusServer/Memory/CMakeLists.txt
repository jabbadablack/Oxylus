# Slot maps, the per-thread scoped stack, TLSF, read guards and hashing.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusServer/Memory -B build/standalone-memory
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusServerMemory LANGUAGES C CXX)
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

add_library(OxylusServerMemory SHARED ${OX_SOURCES})
add_library(ox::OxylusServerMemory ALIAS OxylusServerMemory)
ox_configure_module(OxylusServerMemory)

ox_require_simdutf()

target_link_libraries(OxylusServerMemory
  PUBLIC
    ox_server_headers
    simdutf::simdutf
  PRIVATE
    OxylusServerOS)
