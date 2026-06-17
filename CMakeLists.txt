cmake_minimum_required(VERSION 3.16)

if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
  message(FATAL_ERROR
    "In-source builds are not supported. Use: cmake -S . -B <build-dir>")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(PYTHON3 "${Python3_EXECUTABLE}")

# Find Git for submodule auto-initialization
find_package(Git QUIET)

project(audio_studio
  VERSION 1.0.0
  DESCRIPTION "Audio Studio"
  LANGUAGES CXX C)

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
add_subdirectory(plugins/builtin_module_configs)

# ==============================================================================
# alsatplg - ALSA Topology Compiler
# ==============================================================================
# Build alsatplg using ExternalProject to:
# 1. Keep alsa-utils submodule clean (no modifications)
# 2. Build alsatplg as an independent project
# 3. Handle ALSA dependency detection properly

# Auto-initialize alsa-utils submodule if not present
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/alsa-utils/topology/topology.c")
  message(STATUS "alsa-utils submodule not found, attempting to initialize...")
  if(GIT_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive third_party/alsa-utils
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      RESULT_VARIABLE GIT_SUBMODULE_RESULT
      OUTPUT_QUIET ERROR_QUIET
    )
    if(GIT_SUBMODULE_RESULT EQUAL 0)
      message(STATUS "alsa-utils submodule initialized successfully")
    else()
      message(WARNING "Failed to initialize alsa-utils submodule (git exit code: ${GIT_SUBMODULE_RESULT})")
      message(WARNING "alsatplg will not be built. Run manually: git submodule update --init --recursive")
    endif()
  else()
    message(WARNING "Git not found, cannot auto-initialize alsa-utils submodule")
    message(WARNING "alsatplg will not be built. Run manually: git submodule update --init --recursive")
  endif()
endif()

# Build alsatplg as external project
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/alsatplg/CMakeLists.txt")
  include(ExternalProject)
  ExternalProject_Add(alsatplg
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/alsatplg"
    BINARY_DIR "${CMAKE_BINARY_DIR}/third_party/alsatplg"
    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DALSA_LIB_INCLUDE_DIR=${ALSA_LIB_INCLUDE_DIR}
      -DALSA_LIB_LIBRARY=${ALSA_LIB_LIBRARY}
      -DALSA_TOPOLOGY_LIB_INCLUDE_DIR=${ALSA_TOPOLOGY_LIB_INCLUDE_DIR}
      -DALSA_TOPOLOGY_LIB_LIBRARY=${ALSA_TOPOLOGY_LIB_LIBRARY}
    BUILD_ALWAYS FALSE
    INSTALL_COMMAND ""
  )

  # Create a helper target to find the alsatplg executable
  add_custom_target(alsatplg_available ALL)
  add_dependencies(alsatplg_available alsatplg)
else()
  message(STATUS "alsatplg build not available (third_party/alsatplg/CMakeLists.txt not found)")
endif()

if(CONFIG_RPC_CORE OR CONFIG_FRAMEWORK_CONFIG)
  add_library(audio_studio_json
    rpc/src/json_value.cpp
  )
  target_include_directories(audio_studio_json PUBLIC rpc/include)
  target_compile_features(audio_studio_json PUBLIC cxx_std_17)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

enable_testing()

if(CONFIG_RPC AND NOT CONFIG_SERVER)
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
