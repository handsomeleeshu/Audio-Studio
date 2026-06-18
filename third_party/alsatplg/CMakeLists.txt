cmake_minimum_required(VERSION 3.16)

project(alsatplg C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(ALSA_LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/../alsa-lib")
set(ALSA_UTILS_DIR "${CMAKE_CURRENT_LIST_DIR}/../alsa-utils")

if(NOT EXISTS "${ALSA_LIB_DIR}/src/topology/decoder.c")
  message(FATAL_ERROR "third_party/alsa-lib sources are required")
endif()
if(NOT EXISTS "${ALSA_UTILS_DIR}/topology/topology.c")
  message(FATAL_ERROR "third_party/alsa-utils sources are required")
endif()

file(GLOB_RECURSE ALSA_LIB_SOURCES CONFIGURE_DEPENDS
  "${ALSA_LIB_DIR}/src/*.c")
list(FILTER ALSA_LIB_SOURCES EXCLUDE REGEX ".*/src/pcm/pcm_ladspa\\.c$")
list(FILTER ALSA_LIB_SOURCES EXCLUDE REGEX ".*/src/pcm/pcm_dmix_generic\\.c$")
list(FILTER ALSA_LIB_SOURCES EXCLUDE REGEX ".*/src/pcm/pcm_dmix_i386\\.c$")
list(FILTER ALSA_LIB_SOURCES EXCLUDE REGEX ".*/src/pcm/pcm_dmix_x86_64\\.c$")
list(FILTER ALSA_LIB_SOURCES EXCLUDE REGEX ".*/src/pcm/pcm_symbols_list\\.c$")
list(FILTER ALSA_LIB_SOURCES EXCLUDE REGEX ".*/src/control/ctl_symbols_list\\.c$")
list(FILTER ALSA_LIB_SOURCES EXCLUDE REGEX ".*/src/pcm/scopes/.*\\.c$")

set(ALSA_UTILS_TOPOLOGY_SOURCES
  "${ALSA_UTILS_DIR}/topology/topology.c"
  "${ALSA_UTILS_DIR}/topology/pre-processor.c"
  "${ALSA_UTILS_DIR}/topology/pre-process-class.c"
  "${ALSA_UTILS_DIR}/topology/pre-process-object.c"
  "${ALSA_UTILS_DIR}/topology/pre-process-dapm.c"
  "${ALSA_UTILS_DIR}/topology/pre-process-dai.c")

add_executable(alsatplg
  ${ALSA_UTILS_TOPOLOGY_SOURCES}
  ${ALSA_LIB_SOURCES}
  stubs.c)

target_include_directories(alsatplg PRIVATE
  "${ALSA_LIB_DIR}/include"
  "${CMAKE_CURRENT_LIST_DIR}/include"
  "${ALSA_UTILS_DIR}/include")

target_compile_definitions(alsatplg PRIVATE
  HAVE_CONFIG_H=1
  ALSA_TOPOLOGY_PLUGIN_DIR="/usr/lib/alsa-topology"
  SND_UTIL_MAJOR=1
  SND_UTIL_MINOR=2
  SND_UTIL_SUBMINOR=16
  SND_UTIL_VERSION_STR="1.2.16")

if(MSVC)
  target_compile_options(alsatplg PRIVATE /W3)
else()
  target_compile_options(alsatplg PRIVATE -Wall)
endif()

if(UNIX)
  target_link_libraries(alsatplg PRIVATE m pthread dl rt)
endif()

set_target_properties(alsatplg PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
