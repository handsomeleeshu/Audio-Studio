cmake_minimum_required(VERSION 3.16)

project(audio_controller C)

set(SOF_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../sof" CACHE PATH
  "Path to the VASS SOF source tree")
set(SOF_BUILD_DIR "" CACHE PATH
  "Path to the generated SOF build directory")

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(AUDIO_CONTROLLER_SOURCES
  src/ac_audio_controller.c
  src/ac_audio.c
  src/ac_datalink.c
  src/ac_log.c
  src/ac_transport.c
  src/ac_topology_parser.c
  src/ac_sof_loader.c)

function(audio_controller_apply_common_settings target_name)
  target_include_directories(${target_name} PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include")

  target_include_directories(${target_name} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${SOF_ROOT}/client/include"
    "${SOF_ROOT}/client/src/include"
    "${SOF_ROOT}/src/include")

  if(SOF_BUILD_DIR)
    target_include_directories(${target_name} PRIVATE
      "${SOF_BUILD_DIR}/generated/include")
    target_compile_options(${target_name} PRIVATE
      -imacros${SOF_BUILD_DIR}/generated/include/autoconfig.h)
  endif()

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Werror)
  endif()
endfunction()

add_library(audio_controller STATIC ${AUDIO_CONTROLLER_SOURCES})
audio_controller_apply_common_settings(audio_controller)

option(AUDIO_CONTROLLER_BUILD_TESTS "Build audio_controller unit tests" ON)
if(AUDIO_CONTROLLER_BUILD_TESTS)
  enable_testing()
  add_executable(audio_controller_transport_tests
    tests/ac_transport_tests.c)
  target_include_directories(audio_controller_transport_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${SOF_ROOT}/client/include"
    "${SOF_ROOT}/client/src/include"
    "${SOF_ROOT}/src/include")
  target_link_libraries(audio_controller_transport_tests PRIVATE audio_controller)
  add_test(NAME audio_controller_transport_tests
    COMMAND audio_controller_transport_tests)

  add_library(audio_controller_ptable_for_tests STATIC
    ${AUDIO_CONTROLLER_SOURCES})
  audio_controller_apply_common_settings(audio_controller_ptable_for_tests)
  target_compile_definitions(audio_controller_ptable_for_tests PRIVATE
    CONFIG_HOST_PTABLE=1)

  add_executable(audio_controller_transport_ptable_tests
    tests/ac_transport_tests.c)
  target_compile_definitions(audio_controller_transport_ptable_tests PRIVATE
    CONFIG_HOST_PTABLE=1)
  target_include_directories(audio_controller_transport_ptable_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${SOF_ROOT}/client/include"
    "${SOF_ROOT}/client/src/include"
    "${SOF_ROOT}/src/include")
  target_link_libraries(audio_controller_transport_ptable_tests PRIVATE
    audio_controller_ptable_for_tests)
  add_test(NAME audio_controller_transport_ptable_tests
    COMMAND audio_controller_transport_ptable_tests)
endif()
