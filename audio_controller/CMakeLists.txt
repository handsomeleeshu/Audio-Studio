cmake_minimum_required(VERSION 3.16)

project(audio_controller C)

set(SOF_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../sof" CACHE PATH
  "Path to the VASS SOF source tree")
set(SOF_BUILD_DIR "" CACHE PATH
  "Path to the generated SOF build directory")

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(audio_controller STATIC
  src/ac_audio_controller.c
  src/ac_audio.c
  src/ac_datalink.c
  src/ac_log.c
  src/ac_transport.c
  src/ac_topology_parser.c
  src/ac_sof_loader.c)

target_include_directories(audio_controller PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}/include")

target_include_directories(audio_controller PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/src"
  "${SOF_ROOT}/client/include"
  "${SOF_ROOT}/client/src/include"
  "${SOF_ROOT}/src/include")

if(SOF_BUILD_DIR)
  target_include_directories(audio_controller PRIVATE
    "${SOF_BUILD_DIR}/generated/include")
  target_compile_options(audio_controller PRIVATE
    -imacros${SOF_BUILD_DIR}/generated/include/autoconfig.h)
endif()

if(MSVC)
  target_compile_options(audio_controller PRIVATE /W4)
else()
  target_compile_options(audio_controller PRIVATE -Wall -Wextra -Werror)
endif()

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
endif()
