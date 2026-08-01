# GPU-side assets: textures, glTF import, tilemaps, embedded assets.

cmake_minimum_required(VERSION 3.25)

# Configurable on its own:  cmake -S OxylusClient/Asset -B build/standalone-asset
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(OX_BUILD_TYPES Debug Release Dist)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Oxylus build mode" FORCE)
  endif()
  project(OxylusClientAssetObjects LANGUAGES C CXX)
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

add_library(OxylusClientAssetObjects OBJECT ${OX_SOURCES})
add_library(ox::OxylusClientAssetObjects ALIAS OxylusClientAssetObjects)
ox_configure_module(OxylusClientAssetObjects)

ox_require_glm()
ox_require_unordered_dense()
ox_require_fastgltf()
ox_require_ktx()
ox_require_meshoptimizer()
ox_require_stb()
ox_require_vuk()
ox_require_flecs()
ox_require_zpp_bits()
ox_require_simdjson()
ox_require_svector()
ox_require_vk_bootstrap()

target_link_libraries(OxylusClientAssetObjects
  PUBLIC
    ox_client_headers
    glm::glm-header-only
    unordered_dense::unordered_dense
    fastgltf::fastgltf
    ktx
    meshoptimizer
    ox_stb
    vuk
    Vulkan::Headers
    flecs::flecs
    ox_zpp_bits
    simdjson::simdjson
    svector
    vk-bootstrap::vk-bootstrap)
