# Logging, CVars, math, timing, RNG, JSON writing. The bottom of the server graph: it calls
# into no other module - which is why every other module can depend on it for logging.
#
# This library also compiles loguru itself, so the process has exactly one set of loguru globals
# and Log.cpp - their only writer - sits beside them. See ox_require_loguru() in
# Dependencies.cmake for why a loguru DLL is not an option.
#
# Jolt is linked for OxMath.cpp, which uses JPH::AABox and nothing else. Every Jolt symbol it
# touches is inline, so no Jolt object is pulled into this library and Jolt's global Factory
# stays where it belongs, in OxylusServerLib.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusServer/Utils -B build/standalone-utils
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusServerUtils LANGUAGES C CXX)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
  include(OxBuild)
  include(OxHelpers)
  include(Dependencies)
endif()

ox_module_sources(OX_SOURCES)

ox_require_loguru()

# Third-party, so it does not get the engine's warning flags.
set_source_files_properties("${OX_LOGURU_SOURCE}" PROPERTIES
  COMPILE_OPTIONS "$<IF:$<BOOL:${MSVC}>,/w,-w>")

add_library(OxylusServerUtils SHARED ${OX_SOURCES} "${OX_LOGURU_SOURCE}")
add_library(ox::OxylusServerUtils ALIAS OxylusServerUtils)
ox_configure_module(OxylusServerUtils)

ox_require_glm()
ox_require_unordered_dense()
ox_require_jolt()

target_link_libraries(OxylusServerUtils
  PUBLIC
    ox_server_headers
    glm::glm-header-only
    unordered_dense::unordered_dense
  PRIVATE
    Jolt::Jolt)
