include_guard(GLOBAL)

if(NOT DEFINED OX_REPO_ROOT)
  get_filename_component(_ox_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  set(OX_REPO_ROOT "${_ox_repo_root}" CACHE INTERNAL "Oxylus repository root")
endif()

set(OX_SHADER_SOURCE_ROOT "${OX_REPO_ROOT}/OxylusClient/Render/src/Shaders"
    CACHE INTERNAL "Root directory every shader session TOML resolves against")

set(OX_RESOURCE_EXTENSIONS
  .png .ktx .ktx2 .dds .jpg .mp3 .wav .ogg .otf .ttf
  .lua .txt .glb .gltf .oxasset .oxscene .rml .rcss
  CACHE INTERNAL "Resource extensions staged next to the binary")

function(ox_set_rpath target)
  if(APPLE)
    set(_rpath "@executable_path;@loader_path;@executable_path/../Frameworks")
  elseif(UNIX)
    set(_rpath "$ORIGIN;$ORIGIN/../lib")
  else()
    return()
  endif()
  set_target_properties(${target} PROPERTIES
    BUILD_RPATH "${_rpath}"
    INSTALL_RPATH "${_rpath}"
    BUILD_WITH_INSTALL_RPATH OFF
    INSTALL_RPATH_USE_LINK_PATH ON
    MACOSX_RPATH ON)
endfunction()

function(ox_stage_runtime_deps target)
  ox_set_rpath(${target})

  if(WIN32)
    set(_guard "$<BOOL:$<TARGET_RUNTIME_DLLS:${target}>>")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "$<${_guard}:${CMAKE_COMMAND}>" "$<${_guard}:-E>" "$<${_guard}:copy_if_different>"
              "$<${_guard}:$<TARGET_RUNTIME_DLLS:${target}>>"
              "$<${_guard}:$<TARGET_FILE_DIR:${target}>>"
      COMMAND_EXPAND_LISTS
      VERBATIM)
  endif()

  foreach(_module IN ITEMS slang::slang slang::slang-glslang slang::slang-glsl-module slang::slang-llvm)
    if(TARGET ${_module})
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:${_module}>" "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
    endif()
  endforeach()

  if(OX_SLANG_PREFIX)
    file(GLOB _slang_module_dirs LIST_DIRECTORIES true "${OX_SLANG_PREFIX}/bin/slang-standard-module-*")
    foreach(_dir IN LISTS _slang_module_dirs)
      get_filename_component(_name "${_dir}" NAME)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
                "${_dir}" "$<TARGET_FILE_DIR:${target}>/${_name}"
        VERBATIM)
    endforeach()
  endif()
endfunction()

function(ox_install_resources)
  cmake_parse_arguments(ARG "" "TARGET;ROOT_DIR;OUTPUT_DIR" "EXTENSIONS" ${ARGN})

  foreach(_required TARGET ROOT_DIR OUTPUT_DIR)
    if(NOT ARG_${_required})
      message(FATAL_ERROR "ox_install_resources: ${_required} is required")
    endif()
  endforeach()

  if(NOT ARG_EXTENSIONS)
    set(ARG_EXTENSIONS ${OX_RESOURCE_EXTENSIONS})
  endif()
  get_filename_component(ARG_ROOT_DIR "${ARG_ROOT_DIR}" ABSOLUTE)

  set(_globs "")
  foreach(_ext IN LISTS ARG_EXTENSIONS)
    list(APPEND _globs "${ARG_ROOT_DIR}/*${_ext}")
  endforeach()
  file(GLOB_RECURSE _files CONFIGURE_DEPENDS ${_globs})

  set(_outputs "")
  foreach(_src IN LISTS _files)
    file(RELATIVE_PATH _rel "${ARG_ROOT_DIR}" "${_src}")
    set(_dst "${ARG_OUTPUT_DIR}/${_rel}")
    get_filename_component(_dst_dir "${_dst}" DIRECTORY)

    add_custom_command(
      OUTPUT "${_dst}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${_dst_dir}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_src}" "${_dst}"
      DEPENDS "${_src}"
      COMMENT "Staging resource ${_rel}"
      VERBATIM)
    list(APPEND _outputs "${_dst}")
  endforeach()

  add_custom_target(${ARG_TARGET} DEPENDS ${_outputs})
  if(_files)
    source_group(TREE "${ARG_ROOT_DIR}" PREFIX "Resources" FILES ${_files})
  endif()
endfunction()

function(ox_compile_shaders)
  cmake_parse_arguments(ARG "" "TARGET;CONFIG;OUTPUT_DIR;OUTPUT_NAME" "EXTRA_DEPENDS" ${ARGN})

  foreach(_required TARGET CONFIG OUTPUT_DIR)
    if(NOT ARG_${_required})
      message(FATAL_ERROR "ox_compile_shaders: ${_required} is required")
    endif()
  endforeach()

  get_filename_component(ARG_CONFIG "${ARG_CONFIG}" ABSOLUTE)
  if(NOT EXISTS "${ARG_CONFIG}")
    message(FATAL_ERROR "ox_compile_shaders: no such config '${ARG_CONFIG}'")
  endif()

  if(NOT ARG_OUTPUT_NAME)
    get_filename_component(_base "${ARG_CONFIG}" NAME_WE)
    set(ARG_OUTPUT_NAME "${_base}.oxpack")
  endif()
  set(_output "${ARG_OUTPUT_DIR}/${ARG_OUTPUT_NAME}")

  file(GLOB_RECURSE _slang_sources CONFIGURE_DEPENDS "${OX_SHADER_SOURCE_ROOT}/*.slang")

  add_custom_command(
    OUTPUT "${_output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARG_OUTPUT_DIR}"
    COMMAND "$<TARGET_FILE:rcli>" --config "${ARG_CONFIG}" --output "${_output}"
    DEPENDS rcli "${ARG_CONFIG}" ${_slang_sources} ${ARG_EXTRA_DEPENDS}
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:rcli>"
    COMMENT "Compiling shader pack ${ARG_OUTPUT_NAME}"
    VERBATIM)

  add_custom_target(${ARG_TARGET} DEPENDS "${_output}")
endfunction()

function(_ox_configure_dependency_dir dir)
  # Only these two are skipped wholesale, because neither fetches a package of its own - everything
  # they use is required from the root before they are added.
  #
  # OxylusServer/ and OxylusClient/ are deliberately NOT skipped. Modules call ox_require_*() from
  # their own CMakeLists, so CPM creates targets like ox_imgui and flecs inside a module's directory
  # scope; skipping those directories would leave those packages without their SYSTEM, /w, /O2 and
  # NDEBUG treatment. Getting this wrong is not subtle-but-harmless: ImGui's IM_ASSERT is plain
  # assert(), so dropping NDEBUG turns every ImGui usage assertion back on. Our own targets are
  # excluded individually below, by property rather than by path.
  foreach(_own IN ITEMS OxylusEditor ResourceCompiler)
    if(dir PATH_EQUAL "${CMAKE_SOURCE_DIR}/${_own}")
      return()
    endif()
  endforeach()

  get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_target IN LISTS _targets)
    # ox_engine_config and the two header targets live in the root directory scope alongside the
    # dependencies, so they turn up in this walk. Marking them SYSTEM would hand every engine
    # include directory to the compiler as -isystem and silence the warnings we build for.
    get_target_property(_own ${_target} OX_ENGINE_TARGET)
    if(_own)
      continue()
    endif()

    get_target_property(_type ${_target} TYPE)
    if(NOT _type STREQUAL "UTILITY")
      set_target_properties(${_target} PROPERTIES SYSTEM ON)
    endif()

    if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
      continue()
    endif()

    if(MSVC)
      target_compile_options(${_target} PRIVATE /w $<$<CONFIG:Debug>:/O2>)
    else()
      target_compile_options(${_target} PRIVATE -w $<$<CONFIG:Debug>:-O2>)
    endif()

    target_compile_definitions(${_target} PRIVATE $<$<CONFIG:Debug>:NDEBUG>)
  endforeach()

  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(_subdir IN LISTS _subdirs)
    _ox_configure_dependency_dir("${_subdir}")
  endforeach()
endfunction()

function(ox_configure_dependencies)
  _ox_configure_dependency_dir("${CMAKE_SOURCE_DIR}")
endfunction()

# ---------------------------------------------------------------------------------------------
# Module plumbing
#
# Each subsystem under OxylusServer/ and OxylusClient/ is its own library with its own
# CMakeLists.txt and its own dependency declarations, and can be configured on its own:
#
#   cmake -S OxylusServer/Core -B build/standalone-core
#
# so a module file is included either as a subdirectory of the root project or as the top-level
# project itself. Everything below is written to make those two cases look identical.
# ---------------------------------------------------------------------------------------------

# Modules see each other's public headers through one interface target per half, carrying include
# directories and nothing else.
#
# This is deliberately not modelled with target_link_libraries. Several modules name another
# module's types in their public headers without ever calling into it - Utils/OxMath.hpp includes
# Render/BoundingVolume.hpp, Utils/CVars.hpp includes Core/Types.hpp - and Render links Utils.
# Those are include relationships, not link relationships, and writing them as target dependencies
# would manufacture cycles that do not exist in the object files. Keeping headers on their own
# dependency-free channel leaves target_link_libraries saying exactly one thing: "this module
# references that module's symbols".
function(ox_define_headers_target _name _root)
  if(TARGET ${_name})
    return()
  endif()
  add_library(${_name} INTERFACE)
  file(GLOB _entries LIST_DIRECTORIES true "${_root}/*")
  foreach(_entry IN LISTS _entries)
    if(IS_DIRECTORY "${_entry}/include")
      target_include_directories(${_name} INTERFACE "${_entry}/include")
    endif()
  endforeach()
endfunction()

ox_define_headers_target(ox_server_headers "${OX_REPO_ROOT}/OxylusServer")
ox_define_headers_target(ox_client_headers "${OX_REPO_ROOT}/OxylusClient")
set_target_properties(ox_server_headers ox_client_headers PROPERTIES OX_ENGINE_TARGET TRUE)

# The client half sees the server half's headers; the reverse is what the split exists to prevent.
target_link_libraries(ox_client_headers INTERFACE ox_server_headers)

# What every engine module compiles with. These used to be repeated as PUBLIC properties of the two
# monolithic targets; as an interface target they are written once and reach the modules, the
# executables and the editor alike.
add_library(ox_engine_config INTERFACE)

target_compile_features(ox_engine_config INTERFACE cxx_std_23)

target_compile_definitions(ox_engine_config INTERFACE
  GLM_ENABLE_EXPERIMENTAL
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  $<$<CONFIG:Debug>:OX_DEBUG;_DEBUG>
  $<$<CONFIG:Release>:OX_RELEASE;NDEBUG>
  $<$<CONFIG:Dist>:OX_DISTRIBUTION;NDEBUG>)

if(WIN32)
  target_compile_definitions(ox_engine_config INTERFACE
    _UNICODE
    UNICODE
    WIN32_LEAN_AND_MEAN
    VC_EXTRALEAN
    NOMINMAX
    _WIN32
    _CRT_SECURE_NO_WARNINGS
    OX_PLATFORM_WINDOWS)
elseif(APPLE)
  target_compile_definitions(ox_engine_config INTERFACE OX_PLATFORM_MACOSX)
else()
  target_compile_definitions(ox_engine_config INTERFACE OX_PLATFORM_LINUX)
endif()

if(OX_PROFILE)
  target_compile_definitions(ox_engine_config INTERFACE TRACY_ENABLE=1)
endif()

if(OX_LLVMPIPE)
  target_compile_definitions(ox_engine_config INTERFACE OX_USE_LLVMPIPE=1)
endif()

set_target_properties(ox_engine_config PROPERTIES OX_ENGINE_TARGET TRUE)

if(OX_COMPILER STREQUAL "msvc")
  target_compile_definitions(ox_engine_config INTERFACE OX_COMPILER_MSVC=1)
  target_compile_options(ox_engine_config INTERFACE
    /arch:AVX2 /permissive- /EHsc /bigobj /wd4100 /Zc:preprocessor)
elseif(OX_COMPILER STREQUAL "clang-cl")
  target_compile_definitions(ox_engine_config INTERFACE OX_COMPILER_CLANGCL=1)
  target_compile_options(ox_engine_config INTERFACE
    /arch:AVX2 /permissive- /EHsc /bigobj)
elseif(OX_COMPILER STREQUAL "clang")
  target_compile_definitions(ox_engine_config INTERFACE OX_COMPILER_CLANG=1)
  if(OX_MARCH_NATIVE)
    target_compile_options(ox_engine_config INTERFACE -march=native)
  elseif(OX_ARCH STREQUAL "x86_64")
    target_compile_options(ox_engine_config INTERFACE -march=x86-64-v3)
  endif()
elseif(OX_COMPILER STREQUAL "gcc")
  target_compile_definitions(ox_engine_config INTERFACE OX_COMPILER_GCC=1)
  if(OX_MARCH_NATIVE)
    target_compile_options(ox_engine_config INTERFACE -march=native)
  elseif(OX_ARCH STREQUAL "x86_64")
    target_compile_options(ox_engine_config INTERFACE -march=x86-64-v3)
  endif()
endif()

# Per-target settings that do not propagate through target_link_libraries and so must be applied to
# each engine target individually. Safe on SHARED, OBJECT and STATIC targets; the shared-library
# settings are applied only where they mean something.
function(ox_configure_module _target)
  get_target_property(_type ${_target} TYPE)

  set_target_properties(${_target} PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    OX_ENGINE_TARGET TRUE)

  if(_type STREQUAL "SHARED_LIBRARY")
    # The engine carries no dllexport/dllimport annotations, so the export table is generated
    # rather than declared. Default visibility is not cosmetic here: ModuleRegistry and EventSystem
    # key their maps on std::type_index(typeid(T)), and under hidden visibility typeinfo does not
    # merge across shared objects - App::mod<T>() would quietly fail to find a module another
    # library registered, with no link error to point at it.
    set_target_properties(${_target} PROPERTIES
      WINDOWS_EXPORT_ALL_SYMBOLS ON
      C_VISIBILITY_PRESET default
      CXX_VISIBILITY_PRESET default
      VISIBILITY_INLINES_HIDDEN OFF
      RUNTIME_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}"
      LIBRARY_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}"
      ARCHIVE_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")
    ox_set_rpath(${_target})
  endif()

  target_link_libraries(${_target} PUBLIC ox_engine_config)
  target_link_libraries(${_target} PRIVATE ox_project_options)

  # Guards #ifdef blocks in ComponentRegistry.hpp, Components.cpp and LuaManager.cpp, which live on
  # both sides of the split - so both halves need it.
  if(OX_LUA_BINDINGS)
    target_compile_definitions(${_target} PRIVATE OX_LUA_BINDINGS)
  endif()

  # Three dependencies are unavoidable in every module rather than declared per module.
  #
  # Tracy is force-included into every translation unit below, so its header has to be on the path
  # even when profiling is compiled out. fmt and loguru arrive through Utils/Log.hpp, which defines
  # OX_LOG_* and OX_ASSERT directly in terms of loguru's LOG_F/CHECK_F macros and includes
  # <fmt/std.h> - and Log.hpp is pulled in by Core/Handle.hpp, Core/ModuleRegistry.hpp and
  # Core/EventSystem.hpp, so it reaches essentially every file in the engine.
  ox_require_tracy()
  ox_require_fmt()
  ox_require_loguru()
  target_link_libraries(${_target} PUBLIC Tracy::TracyClient fmt::fmt ox_loguru)

  # ox_loguru above is headers only; loguru's implementation is compiled into OxylusServerUtils, so
  # anything that logs - which is everything - links that too. Utils calls into no other module, so
  # depending on it universally cannot introduce a cycle.
  if(NOT _target STREQUAL "OxylusServerUtils")
    target_link_libraries(${_target} PUBLIC OxylusServerUtils)
  endif()
  if(MSVC)
    target_compile_options(${_target} PRIVATE /FItracy/Tracy.hpp)
  else()
    target_compile_options(${_target} PRIVATE "SHELL:-include tracy/Tracy.hpp")
  endif()
endfunction()

# The glob every module runs over its own src/. Platform and entry-point filtering is left to the
# module that needs it.
function(ox_module_sources _out)
  cmake_parse_arguments(ARG "" "" "EXCLUDE" ${ARGN})
  file(GLOB_RECURSE _sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
  foreach(_pattern IN LISTS ARG_EXCLUDE)
    list(FILTER _sources EXCLUDE REGEX "${_pattern}")
  endforeach()
  if(NOT OX_LUA_BINDINGS)
    list(FILTER _sources EXCLUDE REGEX "/[^/]*Bindings[^/]*\\.cpp$")
  endif()
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_sources})
  set(${_out} "${_sources}" PARENT_SCOPE)
endfunction()
