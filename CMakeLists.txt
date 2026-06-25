cmake_minimum_required(VERSION 3.16)

if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
  message(FATAL_ERROR
    "In-source builds are not supported. Use: cmake -S . -B <build-dir>")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(PYTHON3 "${Python3_EXECUTABLE}")

project(audio_studio
  VERSION 1.0.0
  DESCRIPTION "Audio Studio"
  LANGUAGES CXX)

set(AUDIO_STUDIO_ROOT_SOURCE_DIRECTORY "${PROJECT_SOURCE_DIR}")

execute_process(
  COMMAND ${CMAKE_CXX_COMPILER} --version
  OUTPUT_VARIABLE cxx_version_output
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
string(REGEX MATCH "^[^\r\n]+" CC_VERSION_TEXT "${cxx_version_output}")
if(NOT CC_VERSION_TEXT)
  set(CC_VERSION_TEXT 0)
endif()

set(GENERATED_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIRECTORY}/include")

set(DOT_CONFIG_PATH "${GENERATED_DIRECTORY}/.config")
set(CONFIG_H_PATH "${GENERATED_DIRECTORY}/include/autoconfig.h" CACHE
  PATH "Path to Kconfig generated C header")

include(scripts/cmake/misc.cmake)
include(scripts/cmake/kconfig.cmake)
read_kconfig_config("${DOT_CONFIG_PATH}")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DOT_CONFIG_PATH}")

add_library(audio_studio_config INTERFACE)
add_dependencies(audio_studio_config genconfig)
target_include_directories(audio_studio_config INTERFACE "${GENERATED_DIRECTORY}/include")

add_library(audio_studio_cli11 INTERFACE)
target_include_directories(audio_studio_cli11 INTERFACE "${PROJECT_SOURCE_DIR}/third_party/CLI11/include")

add_subdirectory(plugins/module_config_sdk)

if(CONFIG_RPC_CORE OR CONFIG_FRAMEWORK_CONFIG)
  add_library(audio_studio_json
    rpc/src/json_value.cpp
  )
  target_include_directories(audio_studio_json PUBLIC rpc/include)
  target_compile_features(audio_studio_json PUBLIC cxx_std_17)
  set_target_properties(audio_studio_json PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(CONFIG_FRAMEWORK_CONFIG)
  add_subdirectory(plugins/builtin_module_configs)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

enable_testing()

if(CONFIG_FRAMEWORK_COMMON AND NOT CONFIG_SERVER)
  add_subdirectory(server/framework/common "${CMAKE_BINARY_DIR}/server/framework/common")
endif()

if(CONFIG_RPC AND NOT CONFIG_SERVER)
  if(CONFIG_DRIVERS_CORE OR CONFIG_DRIVER_SOCKET OR CONFIG_DRIVER_PIPE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/drivers" "${CMAKE_BINARY_DIR}/drivers")
  endif()
  add_subdirectory(rpc)
endif()

if(CONFIG_SERVER)
  add_subdirectory(server)
endif()

if(CONFIG_GUI_BACKEND)
  add_subdirectory(GUI/backend)
endif()

if(CONFIG_CLI)
  add_subdirectory(cli)
endif()
