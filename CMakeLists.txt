cmake_minimum_required(VERSION 3.16)
project(audio_studio LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(AUDIO_STUDIO_CONFIG_FILE "" CACHE FILEPATH "Generated Audio Studio config.cmake")
if(AUDIO_STUDIO_CONFIG_FILE)
  include("${AUDIO_STUDIO_CONFIG_FILE}")
else()
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/audio_studio_default_config.cmake")
endif()

enable_testing()

if(CONFIG_GUI_BACKEND)
  add_subdirectory(GUI/backend)
endif()
