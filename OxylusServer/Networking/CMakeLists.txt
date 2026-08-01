# enet transport, packet encoding and the client/server handles.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusServer/Networking -B build/standalone-networking
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusServerNetworkingObjects LANGUAGES C CXX)
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

add_library(OxylusServerNetworkingObjects OBJECT ${OX_SOURCES})
add_library(ox::OxylusServerNetworkingObjects ALIAS OxylusServerNetworkingObjects)
ox_configure_module(OxylusServerNetworkingObjects)

ox_require_unordered_dense()
ox_require_svector()
ox_require_zpp_bits()
ox_require_enet()
ox_require_flecs()

target_link_libraries(OxylusServerNetworkingObjects
  PUBLIC
    ox_server_headers
    unordered_dense::unordered_dense
    svector
    ox_zpp_bits
    enet_static
    flecs::flecs)
