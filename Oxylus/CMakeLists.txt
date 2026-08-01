# The presentation half of the engine: renderer, windowing, input, UI.
#
# It links OxylusServerLib PUBLIC, so it inherits every shared compile definition, option and
# include directory from the server half - which is where the ECS, physics, scripting, assets and
# core services now live (see OxylusServer/CMakeLists.txt).
#
# The split is the directory layout, not a list in this file: anything under Oxylus/ is client-side
# and may use vuk, SDL, ImGui and RmlUi freely.

file(GLOB_RECURSE OX_CLIENT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

if(NOT OX_LUA_BINDINGS)
  list(FILTER OX_CLIENT_SOURCES EXCLUDE REGEX "/src/Scripting/[^/]*Bindings[^/]*\\.cpp$")
endif()

add_library(Oxylus STATIC ${OX_CLIENT_SOURCES})
add_library(ox::Oxylus ALIAS Oxylus)
ox_configure_engine_target(Oxylus)

target_compile_features(Oxylus PUBLIC cxx_std_23)

target_include_directories(Oxylus
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/UI")

target_link_libraries(Oxylus PUBLIC
  OxylusServerLib
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

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${OX_CLIENT_SOURCES})
