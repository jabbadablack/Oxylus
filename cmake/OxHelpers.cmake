include_guard(GLOBAL)

set(OX_SHADER_SOURCE_ROOT "${CMAKE_SOURCE_DIR}/OxylusClient/src/Render/Shaders"
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
  get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_target IN LISTS _targets)
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

# Per-target settings that do not propagate through target_link_libraries and so must be applied
# to each engine target individually.
function(ox_configure_engine_target _target)
  set_target_properties(${_target} PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    ARCHIVE_OUTPUT_DIRECTORY "${OX_OUTPUT_DIR}")

  target_link_libraries(${_target} PRIVATE ox_project_options)

  # Guards #ifdef blocks in ComponentRegistry.hpp, Components.cpp and LuaManager.cpp, which live on
  # both sides of the split - so both targets need it.
  if(OX_LUA_BINDINGS)
    target_compile_definitions(${_target} PRIVATE OX_LUA_BINDINGS)
  endif()

  if(MSVC)
    target_compile_options(${_target} PRIVATE /FItracy/Tracy.hpp)
  else()
    target_compile_options(${_target} PRIVATE "SHELL:-include tracy/Tracy.hpp")
  endif()
endfunction()
