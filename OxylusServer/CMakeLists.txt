# The server half of the engine, and the process that runs it.
#
# Each subsystem under this directory is its own library with its own CMakeLists.txt and its own
# dependency declarations. Six of them are shared libraries in their own right; the other six are
# object libraries aggregated into OxylusServerLib.
#
#   OxylusServerUtils   logging, CVars, math, timing        -- leaf
#   OxylusServerOS      filesystem and OS services          -- leaf
#   OxylusServerAudio   miniaudio engine                    -- leaf
#   OxylusServerMemory  slot maps, scoped stack, TLSF       -> OS
#   OxylusServerCore    registry, events, jobs, VFS         -> Memory, Utils, OS
#   OxylusServerRender  cameras, frusta, renderer CVars     -> Utils
#   OxylusServerLib     Asset + Networking + Physics + Scene + Scripting + Server
#
# That last one is a group rather than six libraries because those six reference each other's
# symbols in a cycle - Scene <-> Server, Scene <-> Scripting, Scene <-> Physics and more. A cycle
# cannot be expressed as separate shared libraries: on Windows each import library would have to
# exist before the other could be linked. They keep their own directories and CMakeLists; the
# aggregation is only how they are linked.
#
# What is deliberately absent from every link line here: Vulkan, vuk, vk-bootstrap, SDL3, ImGui,
# RmlUi, ktx, fastgltf, meshoptimizer, stb. Adding one silently deletes the server/client boundary.
#
#   OxylusServer  the headless executable. Links OxylusServerLib and nothing else from the engine;
#                 if it ever needs ox::Oxylus, the boundary has been broken.

add_subdirectory(Utils)
add_subdirectory(OS)
add_subdirectory(Audio)
add_subdirectory(Memory)
add_subdirectory(Core)
add_subdirectory(Render)

add_subdirectory(Asset)
add_subdirectory(Networking)
add_subdirectory(Physics)
add_subdirectory(Scene)
add_subdirectory(Scripting)
add_subdirectory(Server)

add_library(OxylusServerLib SHARED)
add_library(ox::OxylusServerLib ALIAS OxylusServerLib)
ox_configure_module(OxylusServerLib)

# The target's own object files all arrive through the module libraries below.
set_target_properties(OxylusServerLib PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(OxylusServerLib
  PUBLIC
    OxylusServerAssetObjects
    OxylusServerNetworkingObjects
    OxylusServerPhysicsObjects
    OxylusServerSceneObjects
    OxylusServerScriptingObjects
    OxylusServerServerObjects
    OxylusServerCore
    OxylusServerMemory
    OxylusServerUtils
    OxylusServerOS
    OxylusServerAudio
    OxylusServerRender)

add_executable(OxylusServer "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/server.cpp")

target_compile_features(OxylusServer PRIVATE cxx_std_23)
set_target_properties(OxylusServer PROPERTIES
  CXX_EXTENSIONS OFF
  OX_ENGINE_TARGET TRUE
  RUNTIME_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")

ox_require_fmt()

target_link_libraries(OxylusServer
  PRIVATE
    ox_project_options
    ox::OxylusServerLib
    fmt::fmt)

ox_set_rpath(OxylusServer)
