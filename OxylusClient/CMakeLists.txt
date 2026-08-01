# The presentation half of the engine: renderer, windowing, input, UI.
#
# Same shape as the server half - each subsystem under this directory is its own library with its
# own CMakeLists.txt and its own dependency declarations:
#
#   OxylusClientUtils   colour, ImGui scopes, TOML/JSON helpers, profiler  -- leaf
#   OxylusClientLib     Asset + Core + Render + Scripting + UI
#
# Those five reference each other's symbols in a cycle - Render <-> Core, UI <-> Asset,
# Render <-> UI and more - so they link as one shared library for the same reason the server
# simulation group does. They keep their own directories and CMakeLists regardless.
#
# The split from the server half is the directory layout, not a list in this file: anything under
# OxylusClient/ may use vuk, SDL, ImGui and RmlUi freely. OxylusClientLib links OxylusServerLib, so
# it inherits the whole simulation side; nothing goes the other way.
#
# STATIC, unlike the server half, and deliberately. OxylusEditor is its own executable and uses vuk
# types directly, so vuk ends up linked into whatever links the client - and vuk keeps process-wide
# state behind globals (vuk::Name's intern pool, vuk::current_module). As a DLL that state would be
# duplicated between the client library and the editor, and a vuk::Name interned on one side would
# not match the pipeline map built on the other: std::out_of_range out of vuk, at the first frame.
# vuk cannot be a DLL either (it exports global data with no dllimport macro), so the only
# arrangement that leaves one copy is to let the client and the editor link into one binary.
# The server half has no vuk, no SDL, no ImGui and no RmlUi, which is exactly why it can be DLLs.

add_subdirectory(Utils)
add_subdirectory(Asset)
add_subdirectory(Core)
add_subdirectory(Render)
add_subdirectory(Scripting)
add_subdirectory(UI)

add_library(OxylusClientLib STATIC)

# The editor and every game link this name; it predates the split into modules and stays.
add_library(ox::Oxylus ALIAS OxylusClientLib)
add_library(ox::OxylusClientLib ALIAS OxylusClientLib)
ox_configure_module(OxylusClientLib)

# The target's own object files all arrive through the module libraries below.
set_target_properties(OxylusClientLib PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(OxylusClientLib
  PUBLIC
    OxylusClientAssetObjects
    OxylusClientCoreObjects
    OxylusClientRenderObjects
    OxylusClientScriptingObjects
    OxylusClientUIObjects
    OxylusClientUtils
    ox::OxylusServerLib)

# The game runtime, built by the client half rather than by a target of its own. It is App plus
# DefaultModules with no Editor module - the difference between a game and the editor is one
# module, which is the point.
add_executable(OxylusRuntime "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/runtime.cpp")

target_compile_features(OxylusRuntime PRIVATE cxx_std_23)
set_target_properties(OxylusRuntime PROPERTIES
  CXX_EXTENSIONS OFF
  OX_ENGINE_TARGET TRUE
  RUNTIME_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")

target_link_libraries(OxylusRuntime PRIVATE ox_project_options ox::Oxylus)

ox_set_rpath(OxylusRuntime)
