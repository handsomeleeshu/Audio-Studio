cmake_minimum_required(VERSION 3.16)

project(audio_controller C)

option(AUDIO_CONTROLLER_BUILD_CLI "Build the audio_controller host validation CLI" ON)
option(AUDIO_CONTROLLER_WITH_SOF_CLIENT "Link the real VASS sof_client install path" OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(audio_controller_core STATIC
  src/audio_controller.c
  src/topology_parser.c
  src/sof_loader.c)

target_include_directories(audio_controller_core PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}/include")
target_include_directories(audio_controller_core PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/src")

if(AUDIO_CONTROLLER_WITH_SOF_CLIENT)
  set(SOF_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../sof" CACHE PATH "Path to the VASS SOF source tree")
  target_compile_definitions(audio_controller_core PRIVATE AUDIO_CONTROLLER_WITH_SOF_CLIENT=1)
  target_include_directories(audio_controller_core PRIVATE
    "${SOF_ROOT}/client/include"
    "${SOF_ROOT}/client/src/include"
    "${SOF_ROOT}/src/include"
    "${SOF_ROOT}/src/platform/library/include")
endif()

if(MSVC)
  target_compile_options(audio_controller_core PRIVATE /W4)
else()
  target_compile_options(audio_controller_core PRIVATE -Wall -Wextra -Werror)
endif()

if(AUDIO_CONTROLLER_BUILD_CLI)
  add_executable(audio_controller src/main.c src/platform/host_test.c)
  target_link_libraries(audio_controller PRIVATE audio_controller_core)
  if(MSVC)
    target_compile_options(audio_controller PRIVATE /W4)
  else()
    target_compile_options(audio_controller PRIVATE -Wall -Wextra -Werror)
  endif()
endif()
