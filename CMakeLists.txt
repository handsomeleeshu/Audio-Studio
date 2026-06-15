cmake_minimum_required(VERSION 3.16)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/scripts/cmake")
include(kconfig)
audio_studio_load_platform_config()

project(audio_studio LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

enable_testing()

if(CONFIG_BUILD_GUI_BACKEND STREQUAL "y")
  add_subdirectory(GUI/backend)
endif()

if(CONFIG_BUILD_AS_SERVER STREQUAL "y")
  add_subdirectory(server)
endif()

if(CONFIG_BUILD_CLI STREQUAL "y")
  add_subdirectory(cli)
endif()

if(CONFIG_BUILD_AUDIO_CONTROLLER STREQUAL "y")
  add_subdirectory(audio_controller)
endif()
