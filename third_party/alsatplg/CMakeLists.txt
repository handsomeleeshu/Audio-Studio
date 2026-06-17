# alsatplg - ALSA Topology Compiler
#
# This builds alsatplg from the SOF alsa-utils submodule.
# Source files are referenced from third_party/alsa-utils.

cmake_minimum_required(VERSION 3.16)
project(alsatplg C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Path to alsa-utils source
set(ALSA_UTILS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../alsa-utils")
set(ALSATPLG_SOURCE_DIR "${ALSA_UTILS_SOURCE_DIR}/topology")
set(ALSATPLG_INCLUDE_DIR "${ALSA_UTILS_SOURCE_DIR}/include")

# Validate source directory exists
if(NOT EXISTS "${ALSATPLG_SOURCE_DIR}/topology.c")
  message(FATAL_ERROR "alsatplg source not found at ${ALSATPLG_SOURCE_DIR}")
  message(FATAL_ERROR "Ensure alsa-utils submodule is initialized: git submodule update --init third_party/alsa-utils")
endif()

# Generate minimal aconfig.h (normally created by autotools configure)
set(ACONFIG_CONTENT "#ifndef __ACONFIG_H
#define ____ACONFIG_H

/* Minimal aconfig.h for alsatplg build */

#endif /* __ACONFIG_H */
")
file(WRITE "${ALSATPLG_INCLUDE_DIR}/aconfig.h" "${ACONFIG_CONTENT}")
message(STATUS "Generated ${ALSATPLG_INCLUDE_DIR}/aconfig.h")

# Find ALSA library
set(ALSA_FOUND FALSE)
if(DEFINED ALSA_LIB_INCLUDE_DIR AND DEFINED ALSA_LIB_LIBRARY)
  if(ALSA_LIB_INCLUDE_DIR AND ALSA_LIB_LIBRARY)
    set(ALSA_FOUND TRUE)
    set(ALSA_INCLUDE_DIR ${ALSA_LIB_INCLUDE_DIR})
    set(ALSA_LIBRARY ${ALSA_LIB_LIBRARY})
  endif()
endif()

if(NOT ALSA_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(ALSA libasound)
    if(ALSA_FOUND AND ALSA_INCLUDEDIR AND ALSA_LIBRARIES)
      set(ALSA_INCLUDE_DIR ${ALSA_INCLUDEDIR})
      set(ALSA_LIBRARY ${ALSA_LIBRARIES})
    else()
      set(ALSA_FOUND FALSE)
    endif()
  endif()
endif()

if(NOT ALSA_FOUND)
  find_path(ALSA_INCLUDE_DIR alsa/asoundlib.h)
  find_library(ALSA_LIBRARY asound)
  if(ALSA_INCLUDE_DIR AND ALSA_LIBRARY)
    set(ALSA_FOUND TRUE)
  endif()
endif()

# Find ALSA topology library
set(ALSA_TOPOLOGY_FOUND FALSE)
if(DEFINED ALSA_TOPOLOGY_LIB_INCLUDE_DIR AND DEFINED ALSA_TOPOLOGY_LIB_LIBRARY)
  if(ALSA_TOPOLOGY_LIB_INCLUDE_DIR AND ALSA_TOPOLOGY_LIB_LIBRARY)
    set(ALSA_TOPOLOGY_FOUND TRUE)
    set(ALSA_TOPOLOGY_INCLUDE_DIR ${ALSA_TOPOLOGY_LIB_INCLUDE_DIR})
    set(ALSA_TOPOLOGY_LIBRARY ${ALSA_TOPOLOGY_LIB_LIBRARY})
  endif()
endif()

if(NOT ALSA_TOPOLOGY_FOUND)
  find_path(ALSA_TOPOLOGY_INCLUDE_DIR alsa/topology.h PATH_SUFFIXES alsa)
  find_library(ALSA_TOPOLOGY_LIBRARY atopology)
  if(ALSA_TOPOLOGY_INCLUDE_DIR AND ALSA_TOPOLOGY_LIBRARY)
    set(ALSA_TOPOLOGY_FOUND TRUE)
  endif()
endif()

# Print status
message(STATUS "ALSA library found: ${ALSA_FOUND}")
if(ALSA_FOUND)
  message(STATUS "  ALSA include dir: ${ALSA_INCLUDE_DIR}")
  message(STATUS "  ALSA library: ${ALSA_LIBRARY}")
endif()

message(STATUS "ALSA topology library found: ${ALSA_TOPOLOGY_FOUND}")
if(ALSA_TOPOLOGY_FOUND)
  message(STATUS "  ALSA topology include dir: ${ALSA_TOPOLOGY_INCLUDE_DIR}")
  message(STATUS "  ALSA topology library: ${ALSA_TOPOLOGY_LIBRARY}")
endif()

# Build alsatplg only if both libraries are found
if(ALSA_FOUND AND ALSA_TOPOLOGY_FOUND)
  add_executable(alsatplg
    ${ALSATPLG_SOURCE_DIR}/topology.c
    ${ALSATPLG_SOURCE_DIR}/pre-processor.c
    ${ALSATPLG_SOURCE_DIR}/pre-process-class.c
    ${ALSATPLG_SOURCE_DIR}/pre-process-object.c
    ${ALSATPLG_SOURCE_DIR}/pre-process-dapm.c
    ${ALSATPLG_SOURCE_DIR}/pre-process-dai.c
  )

  target_include_directories(alsatplg PRIVATE
    ${ALSATPLG_INCLUDE_DIR}
    ${ALSA_INCLUDE_DIR}
    ${ALSA_TOPOLOGY_INCLUDE_DIR}
  )

  target_link_libraries(alsatplg PRIVATE
    ${ALSA_LIBRARY}
    ${ALSA_TOPOLOGY_LIBRARY}
  )

  # Set output directory
  set_target_properties(alsatplg PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OUTPUT_NAME "alsatplg"
  )

  install(TARGETS alsatplg DESTINATION bin)

  message(STATUS "alsatplg will be built")
else()
  message(WARNING "ALSA libraries not found, alsatplg will not be built")
  message(WARNING "On Linux: install libasound2-dev and libatopology-dev")
  message(WARNING "On macOS: consider building alsa-lib from source first")
endif()
