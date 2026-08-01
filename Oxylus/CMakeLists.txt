# The engine is built as two static libraries.
#
#   OxylusSim  - the headless simulation half. ECS, physics, scripting, assets, core services.
#                It links no vuk, no SDL, no ImGui and no RmlUi, so a translation unit that
#                reaches for one of them fails to compile. That link line is the only thing
#                enforcing the simulation/presentation boundary - keep it honest.
#   Oxylus     - the presentation half. Renderer, windowing, input, UI. Links OxylusSim PUBLIC,
#                so it inherits every shared compile definition and option from it.
#
# Sources are partitioned by the lists below rather than by moving files, so include paths and
# history stay put. Directories that belong wholly to one side are matched by regex; the handful
# that still split file-by-file are listed explicitly. Shrink those explicit lists as the
# refactor progresses - they are the remaining debt, not the design.

file(GLOB_RECURSE OX_ALL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

list(FILTER OX_ALL_SOURCES EXCLUDE REGEX "/src/OS/(Win32|Linux|MacOS)[^/]*\\.cpp$")
if(WIN32)
  list(APPEND OX_ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Win32.cpp")
elseif(APPLE)
  list(APPEND OX_ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/OS/MacOS.cpp")
else()
  list(APPEND OX_ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux.cpp")
endif()

if(NOT OX_LUA_BINDINGS)
  list(FILTER OX_ALL_SOURCES EXCLUDE REGEX "/src/Scripting/[^/]*Bindings[^/]*\\.cpp$")
endif()

# Directories that belong wholly to the simulation.
set(OX_SIM_DIR_RE "/src/(Memory|OS|Audio)/")

# Files from split directories that are simulation-side today. Everything not matched here or by
# OX_SIM_DIR_RE lands in the presentation target.
set(OX_SIM_FILES
  src/Core/EventSystem.cpp
  src/Core/JobManager.cpp
  src/Core/ModuleRegistry.cpp
  src/Core/UUID.cpp
  src/Core/VFS.cpp
  src/Utils/CVars.cpp
  src/Utils/Log.cpp
  src/Utils/OxMath.cpp
  src/Utils/Random.cpp
  src/Utils/Timer.cpp
  src/Utils/Timestep.cpp)

set(OX_SIM_SOURCES "")
set(OX_CLIENT_SOURCES "")
foreach(_src IN LISTS OX_ALL_SOURCES)
  file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_src}")
  if(_src MATCHES "${OX_SIM_DIR_RE}" OR _rel IN_LIST OX_SIM_FILES)
    list(APPEND OX_SIM_SOURCES "${_src}")
  else()
    list(APPEND OX_CLIENT_SOURCES "${_src}")
  endif()
endforeach()

# Per-target settings that do not propagate through target_link_libraries and so must be applied
# to each engine target individually.
function(ox_configure_engine_target _target)
  set_target_properties(${_target} PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    ARCHIVE_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")

  target_link_libraries(${_target} PRIVATE ox_project_options)

  # Guards #ifdef blocks in ComponentRegistry.hpp, Components.cpp and LuaManager.cpp, which live on
  # both sides of the split - so both targets need it.
  if(OX_LUA_BINDINGS)
    target_compile_definitions(${_target} PRIVATE OX_LUA_BINDINGS)
  endif()

  if(MSVC)
    target_compile_options(${_target} PRIVATE /FItracy/Tracy.hpp)
  else()
    target_compile_options(${_target} PRIVATE "SHELL:-include tracy/Tracy.hpp")
  endif()
endfunction()

add_library(OxylusSim STATIC ${OX_SIM_SOURCES})
add_library(ox::OxylusSim ALIAS OxylusSim)
ox_configure_engine_target(OxylusSim)

target_compile_features(OxylusSim PUBLIC cxx_std_23)

target_include_directories(OxylusSim
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_definitions(OxylusSim PUBLIC
  GLM_ENABLE_EXPERIMENTAL
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  $<$<CONFIG:Debug>:OX_DEBUG;_DEBUG>
  $<$<CONFIG:Release>:OX_RELEASE;NDEBUG>
  $<$<CONFIG:Dist>:OX_DISTRIBUTION;NDEBUG>)

if(WIN32)
  target_compile_definitions(OxylusSim PUBLIC
    _UNICODE
    UNICODE
    WIN32_LEAN_AND_MEAN
    VC_EXTRALEAN
    NOMINMAX
    _WIN32
    _CRT_SECURE_NO_WARNINGS
    OX_PLATFORM_WINDOWS)
elseif(APPLE)
  target_compile_definitions(OxylusSim PUBLIC OX_PLATFORM_MACOSX)
else()
  target_compile_definitions(OxylusSim PUBLIC OX_PLATFORM_LINUX)
endif()

if(OX_PROFILE)
  target_compile_definitions(OxylusSim PUBLIC TRACY_ENABLE=1)
endif()

if(OX_LLVMPIPE)
  target_compile_definitions(OxylusSim PUBLIC OX_USE_LLVMPIPE=1)
endif()

if(OX_COMPILER STREQUAL "msvc")
  target_compile_definitions(OxylusSim PUBLIC OX_COMPILER_MSVC=1)
  target_compile_options(OxylusSim PUBLIC
    /arch:AVX2 /permissive- /EHsc /bigobj /wd4100 /Zc:preprocessor)
elseif(OX_COMPILER STREQUAL "clang-cl")
  target_compile_definitions(OxylusSim PUBLIC OX_COMPILER_CLANGCL=1)
  target_compile_options(OxylusSim PUBLIC
    /arch:AVX2 /permissive- /EHsc /bigobj)
elseif(OX_COMPILER STREQUAL "clang")
  target_compile_definitions(OxylusSim PUBLIC OX_COMPILER_CLANG=1)
  if(OX_MARCH_NATIVE)
    target_compile_options(OxylusSim PUBLIC -march=native)
  elseif(OX_ARCH STREQUAL "x86_64")
    target_compile_options(OxylusSim PUBLIC -march=x86-64-v3)
  endif()
elseif(OX_COMPILER STREQUAL "gcc")
  target_compile_definitions(OxylusSim PUBLIC OX_COMPILER_GCC=1)
  if(OX_MARCH_NATIVE)
    target_compile_options(OxylusSim PUBLIC -march=native)
  elseif(OX_ARCH STREQUAL "x86_64")
    target_compile_options(OxylusSim PUBLIC -march=x86-64-v3)
  endif()
endif()

# Deliberately absent: Vulkan::Headers, vuk, vk-bootstrap, SDL3, ox_imgui, RmlUi, ktx, fastgltf,
# meshoptimizer, ox_stb. Adding one of these here silently deletes the boundary.
target_link_libraries(OxylusSim PUBLIC
  ox_miniaudio
  ox_zpp_bits
  ox_lua
  ox_sol2
  ox_loguru
  enet_static
  flecs::flecs_static
  Jolt::Jolt
  plf_colony
  unordered_dense::unordered_dense
  svector
  Tracy::TracyClient
  fmt::fmt
  simdutf::simdutf
  simdjson::simdjson
  glm::glm-header-only
  tomlplusplus::tomlplusplus)

add_library(Oxylus STATIC ${OX_CLIENT_SOURCES})
add_library(ox::Oxylus ALIAS Oxylus)
ox_configure_engine_target(Oxylus)

target_include_directories(Oxylus PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/UI")

target_link_libraries(Oxylus PUBLIC
  OxylusSim
  Vulkan::Headers
  ox_stb
  ox_imgui
  fastgltf::fastgltf
  meshoptimizer
  SDL3::SDL3-static
  ktx
  vk-bootstrap::vk-bootstrap
  vuk
  RmlUi::RmlUi
  RmlUi::Lua)

ox_set_rpath(Oxylus)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${OX_SIM_SOURCES} ${OX_CLIENT_SOURCES})
