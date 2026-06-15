cmake_minimum_required(VERSION 3.16)
project(audio_studio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(AUDIO_STUDIO_PLATFORM "simulator" CACHE STRING "Audio Studio platform profile")
set(AUDIO_STUDIO_TARGET_OS "" CACHE STRING "Audio Studio target OS hint")
set(AUDIO_STUDIO_DEFCONFIG "" CACHE STRING "Audio Studio defconfig used by scripts/build_all")

option(AUDIO_STUDIO_BUILD_GUI_BACKEND "Build existing GUI Web Backend mock runtime" ON)
option(AUDIO_STUDIO_BUILD_SERVER "Build formal Audio Studio as_server skeleton" ON)
option(AUDIO_STUDIO_BUILD_CLI "Build Audio Studio CLI skeleton tools" ON)
option(AUDIO_STUDIO_BUILD_AUDIO_CONTROLLER "Build Audio Controller peer skeleton" ON)

message(STATUS "Audio Studio platform: ${AUDIO_STUDIO_PLATFORM}")
if(AUDIO_STUDIO_TARGET_OS)
  message(STATUS "Audio Studio target OS: ${AUDIO_STUDIO_TARGET_OS}")
endif()
if(AUDIO_STUDIO_DEFCONFIG)
  message(STATUS "Audio Studio defconfig: ${AUDIO_STUDIO_DEFCONFIG}")
endif()

enable_testing()

if(AUDIO_STUDIO_BUILD_GUI_BACKEND)
  add_subdirectory(GUI/backend)
endif()

if(AUDIO_STUDIO_BUILD_SERVER)
  add_subdirectory(server)
endif()

if(AUDIO_STUDIO_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(AUDIO_STUDIO_BUILD_AUDIO_CONTROLLER)
  add_subdirectory(audio_controller)
endif()
