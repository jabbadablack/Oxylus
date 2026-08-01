# The presentation half of the engine: renderer, windowing, input, UI.
#
# It links OxylusServerLib PUBLIC, so it inherits every shared compile definition, option and
# include directory from the server half - which is where the ECS, physics, scripting, assets and
# core services now live (see OxylusServer/CMakeLists.txt).
#
# The split is the directory layout, not a list in this file: anything under Oxylus/ is client-side
# and may use vuk, SDL, ImGui and RmlUi freely.

file(GLOB_RECURSE OX_CLIENT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# The runtime executable entry point lives in the source tree but must not end up in the library,
# the same way ResourceCompiler excludes cli.cpp from its glob.
list(FILTER OX_CLIENT_SOURCES EXCLUDE REGEX "/src/Core/runtime\.cpp$")

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

# The client spawns this by name, so the name lives in one place.
if(WIN32)
  target_compile_definitions(Oxylus PRIVATE OX_SERVER_EXECUTABLE="OxylusServer.exe")
else()
  target_compile_definitions(Oxylus PRIVATE OX_SERVER_EXECUTABLE="OxylusServer")
endif()

ox_set_rpath(Oxylus)

# The game runtime, built by the client half rather than by a target of its own. It is App plus
# DefaultModules with no Editor module - the difference between a game and the editor is one
# module, which is the point.
add_executable(OxylusRuntime "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/runtime.cpp")

target_compile_features(OxylusRuntime PRIVATE cxx_std_23)
set_target_properties(OxylusRuntime PROPERTIES
  CXX_EXTENSIONS OFF
  RUNTIME_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")

target_link_libraries(OxylusRuntime PRIVATE ox_project_options ox::Oxylus)

ox_set_rpath(OxylusRuntime)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${OX_CLIENT_SOURCES})
