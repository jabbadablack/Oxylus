# miniaudio-backed audio engine. A leaf: it calls into no other module.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusServer/Audio -B build/standalone-audio
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusServerAudio LANGUAGES C CXX)
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

add_library(OxylusServerAudio SHARED ${OX_SOURCES})
add_library(ox::OxylusServerAudio ALIAS OxylusServerAudio)
ox_configure_module(OxylusServerAudio)

ox_require_glm()
ox_require_miniaudio()

target_link_libraries(OxylusServerAudio
  PUBLIC
    ox_server_headers
    glm::glm-header-only
    ox_miniaudio)
