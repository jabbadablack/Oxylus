file(GLOB_RECURSE OX_EDITOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(OxylusEditor ${OX_EDITOR_SOURCES})

target_compile_features(OxylusEditor PRIVATE cxx_std_23)
set_target_properties(OxylusEditor PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(OxylusEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(OxylusEditor SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor")

target_link_libraries(OxylusEditor
  PRIVATE
    ox_project_options
    ox::Oxylus
    ox::ResourceCompiler
    ox_imguizmo)

set(OX_EDITOR_RESOURCE_DIR "${OX_OUTPUT_DIR}/Resources")

ox_install_resources(
  TARGET ox_editor_resources
  ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources"
  OUTPUT_DIR "${OX_EDITOR_RESOURCE_DIR}")

ox_compile_shaders(
  TARGET ox_shaders_engine
  CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/engine.toml"
  OUTPUT_DIR "${OX_EDITOR_RESOURCE_DIR}/Shaders")

ox_compile_shaders(
  TARGET ox_shaders_editor
  CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/editor.toml"
  OUTPUT_DIR "${OX_EDITOR_RESOURCE_DIR}/Shaders")

add_dependencies(OxylusEditor ox_editor_resources ox_shaders_engine ox_shaders_editor)

ox_stage_runtime_deps(OxylusEditor)

add_custom_target(run
  COMMAND "$<TARGET_FILE:OxylusEditor>"
  WORKING_DIRECTORY "${OX_OUTPUT_DIR}"
  USES_TERMINAL)
add_dependencies(run OxylusEditor)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${OX_EDITOR_SOURCES})
