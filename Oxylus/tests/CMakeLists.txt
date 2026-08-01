file(GLOB_RECURSE OX_TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Test*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*/Test*.cpp")

if(NOT OX_TEST_SOURCES)
  message(WARNING "ox: OX_TESTS=ON but no Test*.cpp found under ${CMAKE_CURRENT_SOURCE_DIR}")
  return()
endif()

add_custom_target(ox-tests)

foreach(_src IN LISTS OX_TEST_SOURCES)
  get_filename_component(_name "${_src}" NAME_WE)

  add_executable(${_name} EXCLUDE_FROM_ALL "${_src}")
  add_dependencies(ox-tests ${_name})

  target_compile_features(${_name} PRIVATE cxx_std_23)
  set_target_properties(${_name} PROPERTIES CXX_EXTENSIONS OFF)

  target_include_directories(${_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

  # Tests under tests/Sim/ link the headless half only. That is what makes them a boundary check
  # and not just a test: pulling in anything vuk- or SDL-shaped fails to link here.
  if(_src MATCHES "/tests/Sim/")
    set(_engine_lib ox::OxylusSim)
  else()
    set(_engine_lib ox::Oxylus)
  endif()

  target_link_libraries(${_name} PRIVATE
    ox_project_options
    ${_engine_lib}
    GTest::gtest
    GTest::gmock
    GTest::gtest_main)

  if(WIN32)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
      target_compile_options(${_name} PRIVATE /fsanitize=address)
      target_link_options(${_name} PRIVATE /INCREMENTAL:NO)
    endif()
    target_link_options(${_name} PRIVATE /SUBSYSTEM:CONSOLE)
  else()
    target_compile_options(${_name} PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(${_name} PRIVATE -fsanitize=address,undefined)
  endif()

  ox_stage_runtime_deps(${_name})

  add_test(NAME ${_name}
    COMMAND ${_name} --gmock_verbose=info --gtest_stack_trace_depth=10
    WORKING_DIRECTORY "${OX_OUTPUT_DIR}")

  set(_test_env
    "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")
  if(APPLE)
    list(APPEND _test_env "ASAN_OPTIONS=detect_leaks=0:print_stacktrace=1")
    list(APPEND _test_env "DYLD_LIBRARY_PATH=${OX_OUTPUT_DIR}")
  elseif(UNIX)
    list(APPEND _test_env "ASAN_OPTIONS=detect_leaks=1:print_stacktrace=1")
    list(APPEND _test_env "LD_LIBRARY_PATH=${OX_OUTPUT_DIR}")
  else()
    list(APPEND _test_env "ASAN_OPTIONS=detect_container_overflow=0")
  endif()

  set_tests_properties(${_name} PROPERTIES
    ENVIRONMENT "${_test_env}"
    TIMEOUT 300
    LABELS "unit")
endforeach()
