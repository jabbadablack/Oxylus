include_guard(GLOBAL)

include(CheckCXXCompilerFlag)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(OX_COMPILER "msvc")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(OX_COMPILER "clang-cl")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(OX_COMPILER "clang")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(OX_COMPILER "gcc")
else()
  set(OX_COMPILER "unknown")
  message(WARNING "ox: unrecognised compiler '${CMAKE_CXX_COMPILER_ID}'")
endif()

option(OX_LUA_BINDINGS "Enable Lua bindings" ON)
option(OX_EDITOR "Enable Oxylus Editor project" ON)
option(OX_TESTS "Enable tests" OFF)
option(OX_PROFILE "Enable application wide profiling" OFF)
option(OX_LLVMPIPE "Force LLVMPipe instead of GPU during Vulkan device creation" OFF)
option(OX_MARCH_NATIVE "Build for the host CPU (-march=native)" ON)
option(OX_FORCE_M64 "Force -m64 on compile and link" OFF)

set(OX_CXX_RUNTIME "default" CACHE STRING
  "C++ runtime: default | libcxx-shared | libcxx-static | libstdcxx-shared | libstdcxx-static")
set_property(CACHE OX_CXX_RUNTIME PROPERTY STRINGS
  default libcxx-shared libcxx-static libstdcxx-shared libstdcxx-static)

if(MSVC)
  # The DLL runtime, not the static one. The engine ships as a set of shared libraries that hand
  # std::string, std::vector and std::shared_ptr across their boundaries constantly; with the
  # static CRT every DLL carries its own copy of the allocator, so memory allocated in one and
  # freed in another lands in the wrong heap. The symptom is a debug assertion in debug_heap.cpp,
  # "__acrt_first_block == header", from whichever module happened to do the free. One CRT, one heap.
  #
  # Forced, and set as a normal variable on top of the cache entry, because this is not a preference
  # the build can afford to lose: Jolt writes CMAKE_MSVC_RUNTIME_LIBRARY into the cache with FORCE
  # (see USE_STATIC_MSVC_RUNTIME_LIBRARY in Dependencies.cmake), and a value left in the cache by an
  # earlier configure otherwise survives and silently outranks whatever is set here.
  set(_ox_msvc_runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "${_ox_msvc_runtime}" CACHE STRING "MSVC runtime library" FORCE)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "${_ox_msvc_runtime}")
else()
  if(OX_CXX_RUNTIME MATCHES "^libcxx")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
    add_link_options(-stdlib=libc++)
  elseif(OX_CXX_RUNTIME MATCHES "^libstdcxx")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libstdc++>)
    add_link_options(-stdlib=libstdc++)
  endif()
  if(OX_CXX_RUNTIME MATCHES "static$")
    add_link_options(-static-libstdc++)
    if(NOT APPLE)
      add_link_options(-static-libgcc)
    endif()
  endif()
  if(OX_FORCE_M64)
    add_compile_options(-m64)
    add_link_options(-m64)
  endif()
  if(OX_COMPILER STREQUAL "clang")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>)
    add_link_options(-fexperimental-library)
  endif()
endif()

if(MSVC)
  set(_ox_dist_cflags "/O2 /Ob3 /Oi /Gy /DNDEBUG")
  set(_ox_dist_ldflags "/INCREMENTAL:NO /OPT:REF /OPT:ICF")
  # These are force-cached, so whatever they hold survives into the next configure. Strip the
  # runtime-library flag as well as /RTC1: CMAKE_MSVC_RUNTIME_LIBRARY above is what picks the
  # runtime, and a stale /MTd left in the cache from an earlier configure would quietly outrank it
  # - which is exactly how you end up with one DLL on the static CRT and the rest on the dynamic
  # one, each with its own heap.
  foreach(_ox_flags IN ITEMS CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
    string(REGEX REPLACE "[-/]M[TD]d?" "" ${_ox_flags} "${${_ox_flags}}")
    string(REPLACE "/RTC1" "" ${_ox_flags} "${${_ox_flags}}")
    string(STRIP "${${_ox_flags}}" ${_ox_flags})
    set(${_ox_flags} "${${_ox_flags}}" CACHE STRING "" FORCE)
  endforeach()
elseif(APPLE)
  set(_ox_dist_cflags "-O3 -DNDEBUG -ffunction-sections -fdata-sections")
  set(_ox_dist_ldflags "-Wl,-dead_strip -Wl,-x")
else()
  set(_ox_dist_cflags "-O3 -DNDEBUG -ffunction-sections -fdata-sections")
  set(_ox_dist_ldflags "-Wl,--gc-sections -Wl,-s")
endif()

set(CMAKE_C_FLAGS_DIST "${_ox_dist_cflags}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DIST "${_ox_dist_cflags}" CACHE STRING "" FORCE)
set(CMAKE_RC_FLAGS_DIST "" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_DIST "${_ox_dist_ldflags}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DIST "${_ox_dist_ldflags}" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS_DIST "${_ox_dist_ldflags}" CACHE STRING "" FORCE)
set(CMAKE_STATIC_LINKER_FLAGS_DIST "" CACHE STRING "" FORCE)
mark_as_advanced(
  CMAKE_C_FLAGS_DIST CMAKE_CXX_FLAGS_DIST CMAKE_RC_FLAGS_DIST
  CMAKE_EXE_LINKER_FLAGS_DIST CMAKE_SHARED_LINKER_FLAGS_DIST
  CMAKE_MODULE_LINKER_FLAGS_DIST CMAKE_STATIC_LINKER_FLAGS_DIST)

set(CMAKE_MAP_IMPORTED_CONFIG_DIST Dist Release RelWithDebInfo "")
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Debug Release RelWithDebInfo "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release RelWithDebInfo "")

if(CMAKE_BUILD_TYPE STREQUAL "Dist")
  set(CMAKE_C_VISIBILITY_PRESET hidden)
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)
  set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(OX_PLAT windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(OX_PLAT macosx)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(OX_PLAT linux)
else()
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" OX_PLAT)
endif()

if(APPLE AND CMAKE_OSX_ARCHITECTURES)
  list(GET CMAKE_OSX_ARCHITECTURES 0 _ox_raw_arch)
elseif(MSVC AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
  set(_ox_raw_arch "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
else()
  set(_ox_raw_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
string(TOLOWER "${_ox_raw_arch}" _ox_raw_arch)

if(_ox_raw_arch MATCHES "^(amd64|x86_64|x64)$")
  set(OX_ARCH "x86_64")
  if(OX_PLAT STREQUAL "windows")
    set(OX_ARCH "x64")
  endif()
elseif(_ox_raw_arch MATCHES "^(arm64|aarch64)$")
  set(OX_ARCH "arm64")
else()
  set(OX_ARCH "${_ox_raw_arch}")
endif()

set(OX_ARTIFACT_ROOT "${CMAKE_SOURCE_DIR}/build/${OX_PLAT}/${OX_ARCH}")

get_property(_ox_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_ox_multi_config)
  set(OX_MODE "$<LOWER_CASE:$<CONFIG>>")
  set(OX_OUTPUT_DIR "${OX_ARTIFACT_ROOT}/${OX_MODE}")
  foreach(_cfg IN LISTS OX_BUILD_TYPES)
    string(TOUPPER "${_cfg}" _CFG)
    string(TOLOWER "${_cfg}" _cfg_lower)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${OX_ARTIFACT_ROOT}/${_cfg_lower}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${OX_ARTIFACT_ROOT}/${_cfg_lower}")
    set(CMAKE_PDB_OUTPUT_DIRECTORY_${_CFG} "${OX_ARTIFACT_ROOT}/${_cfg_lower}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/lib/${_cfg_lower}")
  endforeach()
else()
  string(TOLOWER "${CMAKE_BUILD_TYPE}" OX_MODE)
  set(OX_OUTPUT_DIR "${OX_ARTIFACT_ROOT}/${OX_MODE}" CACHE INTERNAL "Oxylus artifact directory")
  file(MAKE_DIRECTORY "${OX_OUTPUT_DIR}")

  set(_ox_stamp "${OX_OUTPUT_DIR}/.ox-toolchain")
  set(_ox_stamp_id "${OX_COMPILER}-${CMAKE_CXX_COMPILER_VERSION}-${OX_CXX_RUNTIME}")
  if(EXISTS "${_ox_stamp}")
    file(READ "${_ox_stamp}" _ox_stamp_prev)
    string(STRIP "${_ox_stamp_prev}" _ox_stamp_prev)
    if(NOT _ox_stamp_prev STREQUAL _ox_stamp_id)
      message(FATAL_ERROR
        "ox: ${OX_OUTPUT_DIR} was produced by '${_ox_stamp_prev}', now configuring '${_ox_stamp_id}'.\n"
        "Remove that directory before switching toolchains.")
    endif()
  endif()
  file(WRITE "${_ox_stamp}" "${_ox_stamp_id}")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

function(_ox_add_supported_flags target scope)
  foreach(_flag IN LISTS ARGN)
    string(MAKE_C_IDENTIFIER "OX_HAS${_flag}" _var)
    set(CMAKE_REQUIRED_FLAGS "-Werror=unknown-warning-option -Werror=unused-command-line-argument")
    check_cxx_compiler_flag("${_flag}" ${_var})
    if(${_var})
      target_compile_options(${target} ${scope} "${_flag}")
    endif()
  endforeach()
endfunction()

add_library(ox_project_options INTERFACE)

if(MSVC)
  target_compile_options(ox_project_options INTERFACE /utf-8)
endif()

if(MSVC)
  target_compile_options(ox_project_options INTERFACE /W4)
else()
  target_compile_options(ox_project_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

_ox_add_supported_flags(ox_project_options INTERFACE
  -Wshadow
  -Wshadow-all
  -Wno-missing-braces
  -Wno-unused-parameter
  -Wno-unused-variable
  -Wno-gnu-line-marker
  -Wno-gnu-anonymous-struct
  -Wno-gnu-zero-variadic-macro-arguments
  -Wno-c2y-extensions)

message(STATUS "ox: ${OX_COMPILER} ${CMAKE_CXX_COMPILER_VERSION} runtime=${OX_CXX_RUNTIME}")
message(STATUS "ox: lua_bindings=${OX_LUA_BINDINGS} editor=${OX_EDITOR} tests=${OX_TESTS} "
               "profile=${OX_PROFILE} llvmpipe=${OX_LLVMPIPE} march_native=${OX_MARCH_NATIVE}")
message(STATUS "ox: ${OX_PLAT}/${OX_ARCH}/${OX_MODE} -> ${OX_OUTPUT_DIR}")
