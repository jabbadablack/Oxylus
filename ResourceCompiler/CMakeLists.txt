file(GLOB_RECURSE OXRC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/private/*.cpp")
list(FILTER OXRC_SOURCES EXCLUDE REGEX "/private/cli\\.cpp$")

add_library(ResourceCompiler SHARED ${OXRC_SOURCES})
add_library(ox::ResourceCompiler ALIAS ResourceCompiler)

target_compile_features(ResourceCompiler PUBLIC cxx_std_23)
set_target_properties(ResourceCompiler PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  WINDOWS_EXPORT_ALL_SYMBOLS OFF)

target_include_directories(ResourceCompiler
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")

target_compile_definitions(ResourceCompiler PRIVATE OXRC_EXPORTS=1)

# The shader compiler is a headless tool: it reads TOML, drives slang and writes .oxpack. Linking
# the simulation half rather than ox::Oxylus keeps vuk, SDL, ImGui and RmlUi out of it entirely.
# tomlplusplus used to arrive through the monolithic engine library's PUBLIC link list. Now that
# the engine is per-subsystem modules, only the ones that actually parse TOML carry it, so this
# names its own dependency.
target_link_libraries(ResourceCompiler
  PRIVATE
    ox_project_options
    ox::OxylusServerLib
    slang::slang
    SPIRV-Tools-opt
    ox_zpp_bits
    tomlplusplus::tomlplusplus)

if(UNIX AND NOT APPLE)
  target_link_options(ResourceCompiler PRIVATE "LINKER:--exclude-libs,ALL")
endif()

ox_set_rpath(ResourceCompiler)

add_executable(rcli
  "${CMAKE_CURRENT_SOURCE_DIR}/private/cli.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/private/ResourceConfig.cpp")

target_compile_features(rcli PRIVATE cxx_std_23)
set_target_properties(rcli PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(rcli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")

target_link_libraries(rcli
  PRIVATE
    ox_project_options
    ox::ResourceCompiler
    ox::OxylusServerLib
    fmt::fmt
    tomlplusplus::tomlplusplus
    slang::slang)

ox_stage_runtime_deps(rcli)
