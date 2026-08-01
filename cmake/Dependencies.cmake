include_guard(GLOBAL)

set(CPM_DOWNLOAD_VERSION 0.43.1)
set(CPM_HASH_SUM "1c40fc102ce9625d7de7eb14f541cab30cc3138dca627f0b0ec40293ce6c2934")
set(CPM_DONT_CREATE_PACKAGE_LOCK ON CACHE INTERNAL "")
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if(NOT COMMAND CPMAddPackage)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cmake")
  file(DOWNLOAD
    "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
    "${CPM_DOWNLOAD_LOCATION}"
    EXPECTED_HASH SHA256=${CPM_HASH_SUM})

  get_filename_component(_ox_cpm_dir "${CMAKE_BINARY_DIR}/cmake" REALPATH)
  if(DEFINED CPM_DIRECTORY AND NOT CPM_DIRECTORY STREQUAL _ox_cpm_dir)
    unset(CPM_DIRECTORY CACHE)
    unset(CPM_VERSION CACHE)
  endif()

  include("${CPM_DOWNLOAD_LOCATION}")

  if(NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR
      "ox: including ${CPM_DOWNLOAD_LOCATION} did not define CPMAddPackage.\n"
      "Delete the build directory and reconfigure.")
  endif()
endif()

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

find_package(Vulkan REQUIRED)

CPMAddPackage(
  NAME fmt
  GITHUB_REPOSITORY fmtlib/fmt
  GIT_TAG 12.1.0
  OPTIONS
    "FMT_INSTALL OFF"
    "FMT_DOC OFF"
    "FMT_TEST OFF"
    "FMT_FUZZ OFF"
    "BUILD_SHARED_LIBS OFF")

set(FETCHCONTENT_SOURCE_DIR_FMT "${fmt_SOURCE_DIR}" CACHE PATH "" FORCE)

CPMAddPackage(
  NAME simdjson
  GITHUB_REPOSITORY simdjson/simdjson
  GIT_TAG v4.2.4
  OPTIONS
    "SIMDJSON_DEVELOPER_MODE OFF"
    "SIMDJSON_ENABLE_THREADS ON"
    "BUILD_SHARED_LIBS OFF")

CPMAddPackage(
  NAME simdutf
  GITHUB_REPOSITORY simdutf/simdutf
  GIT_TAG v8.2.0
  OPTIONS
    "SIMDUTF_TESTS OFF"
    "SIMDUTF_BENCHMARKS OFF"
    "SIMDUTF_TOOLS OFF"
    "SIMDUTF_ICONV OFF"
    "BUILD_SHARED_LIBS OFF")

CPMAddPackage(
  NAME glm
  GITHUB_REPOSITORY g-truc/glm
  GIT_TAG 1.0.3
  OPTIONS
    "GLM_BUILD_LIBRARY OFF"
    "GLM_BUILD_TESTS OFF"
    "GLM_BUILD_INSTALL OFF"
    "GLM_ENABLE_CXX_20 ON")

CPMAddPackage(
  NAME flecs
  GITHUB_REPOSITORY SanderMertens/flecs
  GIT_TAG v4.1.5
  OPTIONS
    "FLECS_STATIC ON"
    "FLECS_SHARED OFF"
    "FLECS_TESTS OFF"
    "FLECS_PIC ON")

CPMAddPackage(
  NAME SDL3
  GITHUB_REPOSITORY libsdl-org/SDL
  GIT_TAG release-3.4.12
  OPTIONS
    "SDL_SHARED OFF"
    "SDL_STATIC ON"
    "SDL_TEST_LIBRARY OFF"
    "SDL_TESTS OFF"
    "SDL_EXAMPLES OFF"
    "SDL_INSTALL OFF"
    "SDL_X11 ON"
    "SDL_WAYLAND OFF")

CPMAddPackage(
  NAME meshoptimizer
  GITHUB_REPOSITORY zeux/meshoptimizer
  GIT_TAG v1.2
  OPTIONS
    "MESHOPT_BUILD_DEMO OFF"
    "MESHOPT_BUILD_TOOLS OFF"
    "MESHOPT_INSTALL OFF"
    "BUILD_SHARED_LIBS OFF")

CPMAddPackage(
  NAME unordered_dense
  GITHUB_REPOSITORY martinus/unordered_dense
  GIT_TAG v4.8.1)

CPMAddPackage(
  NAME svector
  GITHUB_REPOSITORY martinus/svector
  GIT_TAG v1.0.3)

CPMAddPackage(
  NAME tomlplusplus
  GITHUB_REPOSITORY marzer/tomlplusplus
  GIT_TAG v3.4.0)

CPMAddPackage(
  NAME miniaudio
  GITHUB_REPOSITORY mackron/miniaudio
  GIT_TAG 0.11.25
  DOWNLOAD_ONLY YES)
add_library(ox_miniaudio INTERFACE)
target_include_directories(ox_miniaudio SYSTEM INTERFACE "${miniaudio_SOURCE_DIR}")

CPMAddPackage(
  NAME stb
  GITHUB_REPOSITORY nothings/stb
  GIT_TAG 013ac3beddff3dbffafd5177e7972067cd2b5083
  DOWNLOAD_ONLY YES)
add_library(ox_stb INTERFACE)
target_include_directories(ox_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")

CPMAddPackage(
  NAME zpp_bits
  GITHUB_REPOSITORY eyalz800/zpp_bits
  GIT_TAG v4.7.1
  DOWNLOAD_ONLY YES)
add_library(ox_zpp_bits INTERFACE)
target_include_directories(ox_zpp_bits SYSTEM INTERFACE "${zpp_bits_SOURCE_DIR}")

CPMAddPackage(
  NAME lua
  GITHUB_REPOSITORY lua/lua
  GIT_TAG v5.4.7
  DOWNLOAD_ONLY YES)

file(GLOB _ox_lua_sources "${lua_SOURCE_DIR}/*.c")
list(FILTER _ox_lua_sources EXCLUDE REGEX "/(lua|luac|onelua)\\.c$")
add_library(ox_lua STATIC ${_ox_lua_sources})
target_include_directories(ox_lua SYSTEM PUBLIC "${lua_SOURCE_DIR}")
set_target_properties(ox_lua PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
if(UNIX)
  if(APPLE)
    target_compile_definitions(ox_lua PRIVATE LUA_USE_MACOSX)
  else()
    target_compile_definitions(ox_lua PRIVATE LUA_USE_LINUX)
  endif()
  target_link_libraries(ox_lua PUBLIC m ${CMAKE_DL_LIBS})
endif()
if(NOT TARGET Lua::Lua)
  add_library(Lua::Lua INTERFACE IMPORTED GLOBAL)
  set_target_properties(Lua::Lua PROPERTIES
    INTERFACE_LINK_LIBRARIES ox_lua
    INTERFACE_INCLUDE_DIRECTORIES "${lua_SOURCE_DIR}")
endif()

CPMAddPackage(
  NAME sol2
  GITHUB_REPOSITORY ThePhD/sol2
  GIT_TAG c1f95a773c6f8f4fde8ca3efe872e7286afe4444
  DOWNLOAD_ONLY YES)
add_library(ox_sol2 INTERFACE)
target_include_directories(ox_sol2 SYSTEM INTERFACE "${sol2_SOURCE_DIR}/include")
target_link_libraries(ox_sol2 INTERFACE ox_lua)

CPMAddPackage(
  NAME imgui
  GITHUB_REPOSITORY ocornut/imgui
  GIT_TAG 42e91c315534a15133fb08fb8108cfdd515e0912
  DOWNLOAD_ONLY YES)
add_library(ox_imgui STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/imgui_demo.cpp"
  "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp")
target_include_directories(ox_imgui SYSTEM PUBLIC
  "${imgui_SOURCE_DIR}"
  "${imgui_SOURCE_DIR}/misc/cpp")
target_compile_definitions(ox_imgui PUBLIC IMGUI_USE_WCHAR32)
set_target_properties(ox_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

CPMAddPackage(
  NAME imguizmo
  GITHUB_REPOSITORY CedricGuillemet/ImGuizmo
  GIT_TAG df1c30142e7c3fb13c171aaeb328bb338fa7aaa6
  DOWNLOAD_ONLY YES)
add_library(ox_imguizmo STATIC "${imguizmo_SOURCE_DIR}/ImGuizmo.cpp")
target_include_directories(ox_imguizmo SYSTEM PUBLIC "${imguizmo_SOURCE_DIR}")
target_link_libraries(ox_imguizmo PUBLIC ox_imgui)
set_target_properties(ox_imguizmo PROPERTIES POSITION_INDEPENDENT_CODE ON)

CPMAddPackage(
  NAME loguru
  GITHUB_REPOSITORY emilk/loguru
  GIT_TAG v2.1.0
  DOWNLOAD_ONLY YES)
add_library(ox_loguru STATIC "${loguru_SOURCE_DIR}/loguru.cpp")
target_include_directories(ox_loguru SYSTEM PUBLIC "${loguru_SOURCE_DIR}")
target_compile_definitions(ox_loguru PUBLIC LOGURU_USE_FMTLIB=1)
target_link_libraries(ox_loguru PUBLIC fmt::fmt)
set_target_properties(ox_loguru PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(ox_loguru PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

CPMAddPackage(
  NAME tracy
  GITHUB_REPOSITORY wolfpld/tracy
  GIT_TAG v0.13.1
  OPTIONS
    "TRACY_ENABLE ${OX_PROFILE}"
    "TRACY_ON_DEMAND ON"
    "TRACY_CALLSTACK ON"
    "TRACY_NO_CALLSTACK_INLINES ON"
    "TRACY_NO_CODE_TRANSFER OFF"
    "TRACY_NO_EXIT OFF"
    "TRACY_NO_SYSTEM_TRACING OFF"
    "TRACY_STATIC ON")

CPMAddPackage(
  NAME enet
  GITHUB_REPOSITORY zpl-c/enet
  GIT_TAG v2.6.5
  OPTIONS
    "ENET_SHARED OFF"
    "ENET_STATIC ON"
    "ENET_TEST OFF"
    "ENET_USE_MORE_PEERS OFF")

CPMAddPackage(
  NAME fastgltf
  GITHUB_REPOSITORY spnda/fastgltf
  GIT_TAG v0.8.0
  OPTIONS
    "FASTGLTF_USE_CUSTOM_SMALLVECTOR OFF"
    "FASTGLTF_DISABLE_CUSTOM_MEMORY_POOL OFF"
    "FASTGLTF_USE_64BIT_FLOAT OFF"
    "FASTGLTF_COMPILE_AS_CPP20 OFF"
    "FASTGLTF_ENABLE_TESTS OFF"
    "FASTGLTF_ENABLE_EXAMPLES OFF"
    "FASTGLTF_ENABLE_DOCS OFF")

CPMAddPackage(
  NAME ktx
  GITHUB_REPOSITORY KhronosGroup/KTX-Software
  GIT_TAG v4.4.0
  OPTIONS
    "KTX_FEATURE_TESTS OFF"
    "KTX_FEATURE_DOC OFF"
    "KTX_FEATURE_JNI OFF"
    "KTX_FEATURE_PY OFF"
    "KTX_FEATURE_TOOLS OFF"
    "KTX_FEATURE_ETC_UNPACK OFF"
    "KTX_FEATURE_KTX1 ON"
    "KTX_FEATURE_KTX2 ON"
    "KTX_FEATURE_VK_UPLOAD OFF"
    "KTX_FEATURE_GL_UPLOAD OFF"
    "BASISU_SUPPORT_OPENCL OFF"
    "BUILD_SHARED_LIBS OFF")

CPMAddPackage(
  NAME vk-bootstrap
  GITHUB_REPOSITORY charles-lunarg/vk-bootstrap
  GIT_TAG v1.4.354
  OPTIONS
    "VK_BOOTSTRAP_TEST OFF"
    "VK_BOOTSTRAP_INSTALL OFF"
    "BUILD_SHARED_LIBS OFF")

CPMAddPackage(
  NAME JoltPhysics
  GITHUB_REPOSITORY jrouwe/JoltPhysics
  GIT_TAG v5.5.0
  SOURCE_SUBDIR Build
  OPTIONS
    "OVERRIDE_CXX_FLAGS OFF"
    "ENABLE_ALL_WARNINGS OFF"
    "ENABLE_INSTALL OFF"
    "INTERPROCEDURAL_OPTIMIZATION OFF"
    "CPP_RTTI_ENABLED ON"
    "CPP_EXCEPTIONS_ENABLED ON"
    "DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ON"
    "DEBUG_RENDERER_IN_DISTRIBUTION ON"
    "USE_SSE4_1 ON"
    "USE_SSE4_2 ON"
    "USE_AVX ON"
    "USE_AVX2 ON"
    "USE_AVX512 OFF"
    "USE_LZCNT ON"
    "USE_TZCNT ON"
    "BUILD_SHARED_LIBS OFF")

if(TARGET Jolt)
  target_compile_definitions(Jolt PUBLIC $<$<CONFIG:Debug>:JPH_NO_DEBUG>)
endif()

CPMAddPackage(
  NAME freetype
  GITHUB_REPOSITORY freetype/freetype
  GIT_TAG VER-2-13-3
  OPTIONS
    "FT_DISABLE_ZLIB ON"
    "FT_DISABLE_BZIP2 ON"
    "FT_DISABLE_PNG ON"
    "FT_DISABLE_HARFBUZZ ON"
    "FT_DISABLE_BROTLI ON"
    "FT_ENABLE_ERROR_STRINGS ON"
    "BUILD_SHARED_LIBS OFF")

if(TARGET freetype AND NOT TARGET Freetype::Freetype)
  add_library(Freetype::Freetype ALIAS freetype)
endif()

CPMAddPackage(
  NAME RmlUi
  VERSION 6.1.0-f7b297e2
  URL "https://github.com/mikke89/RmlUi/archive/f7b297e2c8fc44c5e85df498dbae91762c0769a5.tar.gz"
  URL_HASH SHA256=a9147f5e2d5873c48ac64ee2d80d609d59a89e53490e24b86094549b12be39cb
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "RMLUI_SAMPLES OFF"
    "RMLUI_LUA_BINDINGS ON"
    "RMLUI_LUA_BINDINGS_LIBRARY lua"
    "RMLUI_COMPILER_OPTIONS OFF"
    "RMLUI_WARNINGS_AS_ERRORS OFF"
    "RMLUI_PRECOMPILED_HEADERS OFF"
    "RMLUI_INSTALL_TARGETS_DIR ."
    "BUILD_TESTING OFF")

CPMAddPackage(
  NAME vuk
  GIT_REPOSITORY https://github.com/martty/vuk.git
  GIT_TAG d68e263806aad7660e05d56339a9540d553e4eba
  OPTIONS
    "VUK_BUILD_EXAMPLES OFF"
    "VUK_BUILD_BENCHMARKS OFF"
    "VUK_BUILD_DOCS OFF"
    "VUK_BUILD_TESTS OFF"
    "VUK_USE_SHADERC OFF"
    "VUK_USE_DXC OFF"
    "VUK_USE_VCC OFF"
    "VUK_USE_SLANG OFF"
    "VUK_USE_VULKAN_SDK ON"
    "VUK_LINK_TO_LOADER ON"
    "VUK_EXTRA OFF"
    "VUK_EXTRA_IMGUI OFF"
    "VUK_EXTRA_INIT OFF"
    "VUK_FAIL_FAST OFF"
    "VUK_DEBUG_ALLOCATIONS OFF")

CPMAddPackage(
  NAME SPIRV-Headers
  GITHUB_REPOSITORY KhronosGroup/SPIRV-Headers
  GIT_TAG vulkan-sdk-1.4.335.0
  OPTIONS
    "SPIRV_HEADERS_SKIP_EXAMPLES ON"
    "SPIRV_HEADERS_SKIP_INSTALL ON")

CPMAddPackage(
  NAME SPIRV-Tools
  GITHUB_REPOSITORY KhronosGroup/SPIRV-Tools
  GIT_TAG vulkan-sdk-1.4.335.0
  OPTIONS
    "SPIRV_SKIP_TESTS ON"
    "SPIRV_SKIP_EXECUTABLES ON"
    "SPIRV_WERROR OFF"
    "SPIRV-Headers_SOURCE_DIR ${SPIRV-Headers_SOURCE_DIR}"
    "BUILD_SHARED_LIBS OFF")

set(OX_SLANG_VERSION "2026.12.2")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_ox_slang_slug "windows-x86_64")
  set(_ox_slang_hash "a234a47e8c499080b28cd55e5490cbcc396754d44f823952607ba25d95d25b94")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OX_ARCH STREQUAL "arm64")
  set(_ox_slang_slug "linux-aarch64")
  set(_ox_slang_hash "42e2c649e5b7d1e05e466210ee3314232538604053323d1e3e2f32af81faef08")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_ox_slang_slug "linux-x86_64")
  set(_ox_slang_hash "5533415953112ddeb0a935755bdd2da5de530e6528a560a32ad809c9d9faf29c")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND OX_ARCH STREQUAL "arm64")
  set(_ox_slang_slug "macos-aarch64")
  set(_ox_slang_hash "de919ef0d616a8dba86fa8443bb25975492936872cb261094c1a152522b3b495")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(_ox_slang_slug "macos-x86_64")
  set(_ox_slang_hash "e0bdbd8cc39c8d0b9f7a0308d93f4f5d004af27d71aa131d7b173768fe3f70eb")
else()
  message(FATAL_ERROR "ox: no prebuilt shader-slang for ${CMAKE_SYSTEM_NAME}/${OX_ARCH}")
endif()

if(CPM_SOURCE_CACHE)
  set(_ox_slang_root "${CPM_SOURCE_CACHE}")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
  set(_ox_slang_root "$ENV{CPM_SOURCE_CACHE}")
else()
  set(_ox_slang_root "${CMAKE_BINARY_DIR}/_deps")
endif()
set(_ox_slang_prefix "${_ox_slang_root}/shader-slang-${OX_SLANG_VERSION}-${_ox_slang_slug}")
if(NOT EXISTS "${_ox_slang_prefix}/cmake/slangConfig.cmake")
  file(MAKE_DIRECTORY "${_ox_slang_prefix}")
  message(STATUS "ox: downloading shader-slang ${OX_SLANG_VERSION} (${_ox_slang_slug})")
  file(DOWNLOAD
    "https://github.com/shader-slang/slang/releases/download/v${OX_SLANG_VERSION}/slang-${OX_SLANG_VERSION}-${_ox_slang_slug}.tar.gz"
    "${_ox_slang_prefix}/slang.tar.gz"
    EXPECTED_HASH SHA256=${_ox_slang_hash}
    SHOW_PROGRESS)
  file(ARCHIVE_EXTRACT INPUT "${_ox_slang_prefix}/slang.tar.gz" DESTINATION "${_ox_slang_prefix}")
endif()

find_package(slang REQUIRED CONFIG PATHS "${_ox_slang_prefix}/cmake" NO_DEFAULT_PATH)
set(OX_SLANG_PREFIX "${_ox_slang_prefix}" CACHE INTERNAL "shader-slang install prefix")

if(OX_TESTS)
  CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG v1.17.0
    OPTIONS
      "INSTALL_GTEST OFF"
      "BUILD_GMOCK ON"
      "gtest_force_shared_crt ON"
      "BUILD_SHARED_LIBS OFF")
endif()

ox_configure_dependencies()
