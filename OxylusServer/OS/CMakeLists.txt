# Filesystem and OS services. A leaf: it calls into no other module.
#
# The platform sources are picked here rather than globbed - the same file name pattern the
# monolithic build used, now scoped to the module that owns it.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusServer/OS -B build/standalone-os
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusServerOS LANGUAGES C CXX)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
  include(OxBuild)
  include(OxHelpers)
  include(Dependencies)

  # Every module logs, and loguru's implementation is compiled into this one.
  add_subdirectory(
    "${OX_REPO_ROOT}/OxylusServer/Utils"
    "${CMAKE_BINARY_DIR}/OxylusServerUtils")
endif()

# Platform sources are selected, not globbed.
ox_module_sources(OX_SOURCES EXCLUDE "/src/(Win32|Linux|MacOS)[^/]*\\.cpp$")
if(WIN32)
  list(APPEND OX_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Win32.cpp")
elseif(APPLE)
  list(APPEND OX_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/MacOS.cpp")
else()
  list(APPEND OX_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Linux.cpp")
endif()

add_library(OxylusServerOS SHARED ${OX_SOURCES})
add_library(ox::OxylusServerOS ALIAS OxylusServerOS)
ox_configure_module(OxylusServerOS)

target_link_libraries(OxylusServerOS
  PUBLIC
    ox_server_headers)
