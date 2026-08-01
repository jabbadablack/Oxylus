# The server half of the engine, and the process that runs it.
#
#   OxylusServerLib - ECS, physics, scripting, assets, networking, core services. It links no vuk,
#                     no SDL, no ImGui and no RmlUi, so a translation unit that reaches for one of
#                     them fails to compile. That link line is what enforces the server/client
#                     boundary - keep it honest.
#   OxylusServer    - the headless executable. Links OxylusServerLib and nothing else from the
#                     engine; if it ever needs ox::Oxylus, the boundary has been broken.
#
# Everything here is server-side by virtue of living in this directory. There is no per-file
# partition list any more - the filesystem is the partition.

file(GLOB_RECURSE OX_SERVER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# The executable entry point lives in the source tree but must not end up in the library - the
# same reason ResourceCompiler excludes cli.cpp from its glob.
list(FILTER OX_SERVER_SOURCES EXCLUDE REGEX "/src/Core/server\.cpp$")

list(FILTER OX_SERVER_SOURCES EXCLUDE REGEX "/src/OS/(Win32|Linux|MacOS)[^/]*\\.cpp$")
if(WIN32)
  list(APPEND OX_SERVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Win32.cpp")
elseif(APPLE)
  list(APPEND OX_SERVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/OS/MacOS.cpp")
else()
  list(APPEND OX_SERVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux.cpp")
endif()

if(NOT OX_LUA_BINDINGS)
  list(FILTER OX_SERVER_SOURCES EXCLUDE REGEX "/src/Scripting/[^/]*Bindings[^/]*\\.cpp$")
endif()

add_library(OxylusServerLib STATIC ${OX_SERVER_SOURCES})
add_library(ox::OxylusServerLib ALIAS OxylusServerLib)
ox_configure_engine_target(OxylusServerLib)

target_compile_features(OxylusServerLib PUBLIC cxx_std_23)

target_include_directories(OxylusServerLib
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_definitions(OxylusServerLib PUBLIC
  GLM_ENABLE_EXPERIMENTAL
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  $<$<CONFIG:Debug>:OX_DEBUG;_DEBUG>
  $<$<CONFIG:Release>:OX_RELEASE;NDEBUG>
  $<$<CONFIG:Dist>:OX_DISTRIBUTION;NDEBUG>)

if(WIN32)
  target_compile_definitions(OxylusServerLib PUBLIC
    _UNICODE
    UNICODE
    WIN32_LEAN_AND_MEAN
    VC_EXTRALEAN
    NOMINMAX
    _WIN32
    _CRT_SECURE_NO_WARNINGS
    OX_PLATFORM_WINDOWS)
elseif(APPLE)
  target_compile_definitions(OxylusServerLib PUBLIC OX_PLATFORM_MACOSX)
else()
  target_compile_definitions(OxylusServerLib PUBLIC OX_PLATFORM_LINUX)
endif()

if(OX_PROFILE)
  target_compile_definitions(OxylusServerLib PUBLIC TRACY_ENABLE=1)
endif()

if(OX_LLVMPIPE)
  target_compile_definitions(OxylusServerLib PUBLIC OX_USE_LLVMPIPE=1)
endif()

if(OX_COMPILER STREQUAL "msvc")
  target_compile_definitions(OxylusServerLib PUBLIC OX_COMPILER_MSVC=1)
  target_compile_options(OxylusServerLib PUBLIC
    /arch:AVX2 /permissive- /EHsc /bigobj /wd4100 /Zc:preprocessor)
elseif(OX_COMPILER STREQUAL "clang-cl")
  target_compile_definitions(OxylusServerLib PUBLIC OX_COMPILER_CLANGCL=1)
  target_compile_options(OxylusServerLib PUBLIC
    /arch:AVX2 /permissive- /EHsc /bigobj)
elseif(OX_COMPILER STREQUAL "clang")
  target_compile_definitions(OxylusServerLib PUBLIC OX_COMPILER_CLANG=1)
  if(OX_MARCH_NATIVE)
    target_compile_options(OxylusServerLib PUBLIC -march=native)
  elseif(OX_ARCH STREQUAL "x86_64")
    target_compile_options(OxylusServerLib PUBLIC -march=x86-64-v3)
  endif()
elseif(OX_COMPILER STREQUAL "gcc")
  target_compile_definitions(OxylusServerLib PUBLIC OX_COMPILER_GCC=1)
  if(OX_MARCH_NATIVE)
    target_compile_options(OxylusServerLib PUBLIC -march=native)
  elseif(OX_ARCH STREQUAL "x86_64")
    target_compile_options(OxylusServerLib PUBLIC -march=x86-64-v3)
  endif()
endif()

# Deliberately absent: Vulkan::Headers, vuk, vk-bootstrap, SDL3, ox_imgui, RmlUi, ktx, fastgltf,
# meshoptimizer, ox_stb. Adding one of these here silently deletes the boundary.
target_link_libraries(OxylusServerLib PUBLIC
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

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${OX_SERVER_SOURCES})

add_executable(OxylusServer "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/server.cpp")

target_compile_features(OxylusServer PRIVATE cxx_std_23)
set_target_properties(OxylusServer PROPERTIES
  CXX_EXTENSIONS OFF
  RUNTIME_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")

target_link_libraries(OxylusServer
  PRIVATE
    ox_project_options
    ox::OxylusServerLib
    fmt::fmt)

ox_set_rpath(OxylusServer)
