cmake_minimum_required(VERSION 3.25)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(OX_BUILD_TYPES Debug Release Dist)
if(CMAKE_GENERATOR MATCHES "Visual Studio|Xcode|Multi-Config")
  set(CMAKE_CONFIGURATION_TYPES "${OX_BUILD_TYPES}" CACHE STRING "Oxylus build modes" FORCE)
else()
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${OX_BUILD_TYPES})
  if(NOT CMAKE_BUILD_TYPE IN_LIST OX_BUILD_TYPES)
    message(FATAL_ERROR
      "CMAKE_BUILD_TYPE must be one of: ${OX_BUILD_TYPES} (got '${CMAKE_BUILD_TYPE}').\n"
      "Use a preset, for example: cmake --preset clang-dist")
  endif()
endif()

project(Oxylus VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(OxBuild)
include(OxHelpers)
include(Dependencies)

# The server half first: Oxylus links OxylusServerLib, so its alias has to exist by then.
add_subdirectory(OxylusServer)
add_subdirectory(OxylusClient)
add_subdirectory(ResourceCompiler)

if(OX_EDITOR)
  add_subdirectory(OxylusEditor)
endif()

if(OX_TESTS)
  enable_testing()
  add_subdirectory(OxylusClient/tests)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  add_custom_target(ox_compile_commands ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/build/compile_commands.json"
    COMMENT "Updating build/compile_commands.json"
    VERBATIM)
endif()
