# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.16)

if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
  message(FATAL_ERROR
    "In-source builds are not supported.\n"
    "Please remove CMakeCache.txt/CMakeFiles and use scripts/build_all or cmake -B <build-dir>."
  )
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON3 "${Python3_EXECUTABLE}")

if(NOT DEFINED ARCH)
  set(ARCH host CACHE STRING "Audio Studio build architecture")
endif()

set(AUDIO_STUDIO_PLATFORM "simulator" CACHE STRING "Audio Studio platform profile")
set(AUDIO_STUDIO_TARGET_OS "linux" CACHE STRING "Audio Studio target OS hint")
set(AUDIO_STUDIO_TOOLCHAIN "default" CACHE STRING "Audio Studio toolchain hint")

project(audio_studio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(AUDIO_STUDIO_ROOT_SOURCE_DIRECTORY "${PROJECT_SOURCE_DIR}")
set(AUDIO_STUDIO_ROOT_BINARY_DIRECTORY "${PROJECT_BINARY_DIR}")

execute_process(
  COMMAND ${CMAKE_C_COMPILER} --version
  OUTPUT_VARIABLE cc_version_output
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
string(REGEX MATCH "^[^\r\n]+" CC_VERSION_TEXT "${cc_version_output}")
if(NOT CC_VERSION_TEXT)
  set(CC_VERSION_TEXT "unknown")
endif()

set(GENERATED_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIRECTORY}/include/generated")

set(DOT_CONFIG_PATH "${GENERATED_DIRECTORY}/.config")
set(CONFIG_H_PATH "${GENERATED_DIRECTORY}/include/generated/autoconf.h" CACHE PATH
  "Path to generated Kconfig autoconf.h")

include(scripts/cmake/misc.cmake)
include(scripts/cmake/kconfig.cmake)
read_kconfig_config(${DOT_CONFIG_PATH})

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DOT_CONFIG_PATH})

add_library(audio_studio_options INTERFACE)
add_dependencies(audio_studio_options genconfig)
target_include_directories(audio_studio_options INTERFACE "${GENERATED_DIRECTORY}/include")
target_compile_definitions(audio_studio_options INTERFACE AUDIO_STUDIO_HAS_KCONFIG=1)

message(STATUS "Audio Studio arch: ${ARCH}")
message(STATUS "Audio Studio platform: ${CONFIG_AUDIO_STUDIO_PLATFORM_NAME}")
message(STATUS "Audio Studio target OS: ${CONFIG_AUDIO_STUDIO_TARGET_OS}")
message(STATUS "Audio Studio toolchain: ${AUDIO_STUDIO_TOOLCHAIN}")
message(STATUS "Audio Studio active config: ${DOT_CONFIG_PATH}")
message(STATUS "Audio Studio generated header: ${CONFIG_H_PATH}")

enable_testing()

if(CONFIG_AUDIO_STUDIO_BUILD_GUI_BACKEND)
  add_subdirectory(GUI/backend)
endif()

if(CONFIG_AUDIO_STUDIO_BUILD_SERVER)
  add_subdirectory(server)
endif()

if(CONFIG_AUDIO_STUDIO_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(CONFIG_AUDIO_STUDIO_BUILD_AUDIO_CONTROLLER)
  add_subdirectory(audio_controller)
endif()
