file(GLOB_RECURSE OX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

list(FILTER OX_SOURCES EXCLUDE REGEX "/src/OS/(Win32|Linux|MacOS)[^/]*\\.cpp$")
if(WIN32)
  list(APPEND OX_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Win32.cpp")
elseif(APPLE)
  list(APPEND OX_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/OS/MacOS.cpp")
else()
  list(APPEND OX_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux.cpp")
endif()

if(NOT OX_LUA_BINDINGS)
  list(FILTER OX_SOURCES EXCLUDE REGEX "/src/Scripting/[^/]*Bindings[^/]*\\.cpp$")
endif()

add_library(Oxylus STATIC ${OX_SOURCES})
add_library(ox::Oxylus ALIAS Oxylus)

target_compile_features(Oxylus PUBLIC cxx_std_23)
set_target_properties(Oxylus PROPERTIES
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  ARCHIVE_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")

target_link_libraries(Oxylus PRIVATE ox_project_options)

target_include_directories(Oxylus
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/UI")

target_compile_definitions(Oxylus PUBLIC
  GLM_ENABLE_EXPERIMENTAL
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  $<$<CONFIG:Debug>:OX_DEBUG;_DEBUG>
  $<$<CONFIG:Release>:OX_RELEASE;NDEBUG>
  $<$<CONFIG:Dist>:OX_DISTRIBUTION;NDEBUG>)

if(WIN32)
  target_compile_definitions(Oxylus PUBLIC
    _UNICODE
    UNICODE
    WIN32_LEAN_AND_MEAN
    VC_EXTRALEAN
    NOMINMAX
    _WIN32
    _CRT_SECURE_NO_WARNINGS
    OX_PLATFORM_WINDOWS)
elseif(APPLE)
  target_compile_definitions(Oxylus PUBLIC OX_PLATFORM_MACOSX)
else()
  target_compile_definitions(Oxylus PUBLIC OX_PLATFORM_LINUX)
endif()

if(OX_PROFILE)
  target_compile_definitions(Oxylus PUBLIC TRACY_ENABLE=1)
endif()

if(OX_LLVMPIPE)
  target_compile_definitions(Oxylus PUBLIC OX_USE_LLVMPIPE=1)
endif()

if(OX_LUA_BINDINGS)
  target_compile_definitions(Oxylus PRIVATE OX_LUA_BINDINGS)
endif()

if(OX_COMPILER STREQUAL "msvc")
  target_compile_definitions(Oxylus PUBLIC OX_COMPILER_MSVC=1)
  target_compile_options(Oxylus PUBLIC
    /arch:AVX2 /permissive- /EHsc /bigobj /wd4100 /Zc:preprocessor)
elseif(OX_COMPILER STREQUAL "clang-cl")
  target_compile_definitions(Oxylus PUBLIC OX_COMPILER_CLANGCL=1)
  target_compile_options(Oxylus PUBLIC
    /arch:AVX2 /permissive- /EHsc /bigobj)
elseif(OX_COMPILER STREQUAL "clang")
  target_compile_definitions(Oxylus PUBLIC OX_COMPILER_CLANG=1)
  if(OX_MARCH_NATIVE)
    target_compile_options(Oxylus PUBLIC -march=native)
  elseif(OX_ARCH STREQUAL "x86_64")
    target_compile_options(Oxylus PUBLIC -march=x86-64-v3)
  endif()
elseif(OX_COMPILER STREQUAL "gcc")
  target_compile_definitions(Oxylus PUBLIC OX_COMPILER_GCC=1)
  if(OX_MARCH_NATIVE)
    target_compile_options(Oxylus PUBLIC -march=native)
  elseif(OX_ARCH STREQUAL "x86_64")
    target_compile_options(Oxylus PUBLIC -march=x86-64-v3)
  endif()
endif()

if(MSVC)
  target_compile_options(Oxylus PRIVATE /FItracy/Tracy.hpp)
else()
  target_compile_options(Oxylus PRIVATE "SHELL:-include tracy/Tracy.hpp")
endif()

target_link_libraries(Oxylus PUBLIC
  Vulkan::Headers
  ox_stb
  ox_miniaudio
  ox_zpp_bits
  ox_imgui
  ox_lua
  ox_sol2
  ox_loguru
  fastgltf::fastgltf
  meshoptimizer
  SDL3::SDL3-static
  ktx
  enet_static
  flecs::flecs_static
  vk-bootstrap::vk-bootstrap
  vuk
  plf_colony
  unordered_dense::unordered_dense
  svector
  Tracy::TracyClient
  fmt::fmt
  simdutf::simdutf
  simdjson::simdjson
  Jolt::Jolt
  glm::glm-header-only
  tomlplusplus::tomlplusplus
  RmlUi::RmlUi
  RmlUi::Lua)

ox_set_rpath(Oxylus)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${OX_SOURCES})
