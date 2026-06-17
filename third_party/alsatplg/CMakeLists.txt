# alsatplg - ALSA Topology Compiler
#
# This script provides alsatplg for Audio Studio.
# It prefers the system alsatplg if available, otherwise builds from submodule.

cmake_minimum_required(VERSION 3.16)
project(alsatplg NONE)

# Check for system alsatplg first
find_program(SYSTEM_ALSATPLG alsatplg)
if(SYSTEM_ALSATPLG)
  message(STATUS "Using system alsatplg: ${SYSTEM_ALSATPLG}")

  # Copy to bin directory for consistent path
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${SYSTEM_ALSATPLG} ${CMAKE_BINARY_DIR}/bin/alsatplg
    RESULT_VARIABLE SYMLINK_RESULT
  )
  if(SYMLINK_RESULT EQUAL 0)
    message(STATUS "Symlinked system alsatplg to ${CMAKE_BINARY_DIR}/bin/alsatplg")
  else()
    # Fallback to copy
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E copy ${SYSTEM_ALSATPLG} ${CMAKE_BINARY_DIR}/bin/alsatplg
      RESULT_VARIABLE COPY_RESULT
    )
    if(COPY_RESULT EQUAL 0)
      message(STATUS "Copied system alsatplg to ${CMAKE_BINARY_DIR}/bin/alsatplg")
    endif()
  endif()

  # Create imported target
  add_executable(alsatplg IMPORTED GLOBAL)
  set_target_properties(alsatplg PROPERTIES IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/bin/alsatplg)
elseif(CMAKE_SKIP_BUILD_RULES)
  message(STATUS "Skipping alsatplg build (CMAKE_SKIP_BUILD_RULES set)")
  message(STATUS "alsatplg will be used from system path at runtime")
else()
  message(STATUS "System alsatplg not found, attempting to build from submodule...")

  # Path to alsa-utils source
  set(ALSA_UTILS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../alsa-utils")
  set(ALSATPLG_SOURCE_DIR "${ALSA_UTILS_SOURCE_DIR}/topology")

  # Validate source directory exists
  if(NOT EXISTS "${ALSATPLG_SOURCE_DIR}/topology.c")
    message(FATAL_ERROR "alsatplg source not found at ${ALSATPLG_SOURCE_DIR}")
    message(FATAL_ERROR "Ensure alsa-utils submodule is initialized: git submodule update --init third_party/alsa-utils")
  endif()

  # Check if gitcompile exists
  set(GITCOMPILE_SCRIPT "${ALSA_UTILS_SOURCE_DIR}/gitcompile")
  if(NOT EXISTS "${GITCOMPILE_SCRIPT}")
    message(FATAL_ERROR "gitcompile not found at ${GITCOMPILE_SCRIPT}")
  endif()

  # Check if we need to run gitcompile
  set(CONFIGURE_SCRIPT "${ALSA_UTILS_SOURCE_DIR}/configure")
  if(NOT EXISTS "${CONFIGURE_SCRIPT}")
    set(NEED_GITCOMPILE TRUE)
    message(STATUS "configure not found, will run gitcompile")
  else()
    set(NEED_GITCOMPILE FALSE)
    message(STATUS "configure exists, skipping gitcompile")
  endif()

  # Check for required autotools tools
  find_program(GETTEXTIZE_EXECUTABLE gettextize)
  find_program(LIBTOOLIZE_EXECUTABLE libtoolize)
  find_program(AUTOCONF_EXECUTABLE autoconf)
  find_program(AUTOMAKE_EXECUTABLE automake)

  if(NOT GETTEXTIZE_EXECUTABLE OR NOT LIBTOOLIZE_EXECUTABLE OR NOT AUTOCONF_EXECUTABLE OR NOT AUTOMAKE_EXECUTABLE)
    message(WARNING "Missing autotools tools, cannot build alsatplg from source")
    message(WARNING "Required: gettextize, libtoolize, autoconf, automake")
    message(WARNING "On Ubuntu: sudo apt-get install gettext libtool autoconf automake")
    message(WARNING "On macOS: brew install gettext libtool autoconf automake")
    message(FATAL_ERROR "Cannot build alsatplg without autotools")
  endif()

  # Run gitcompile if needed
  if(NEED_GITCOMPILE)
    execute_process(
      COMMAND ${GITCOMPILE_SCRIPT}
      WORKING_DIRECTORY ${ALSA_UTILS_SOURCE_DIR}
      RESULT_VARIABLE GITCOMPILE_RESULT
      OUTPUT_VARIABLE GITCOMPILE_OUTPUT
      ERROR_VARIABLE GITCOMPILE_ERROR
    )
    if(GITCOMPILE_RESULT EQUAL 0)
      message(STATUS "gitcompile completed")
    else()
      message(FATAL_ERROR "gitcompile failed (exit code ${GITCOMPILE_RESULT})")
      message(STATUS "Output: ${GITCOMPILE_OUTPUT}")
      message(STATUS "Error: ${GITCOMPILE_ERROR}")
    endif()
  endif()

  # Configure alsa-utils (only topology needed)
  set(ALSA_UTILS_BUILD_DIR "${CMAKE_BINARY_DIR}/alsa-utils-build")
  file(MAKE_DIRECTORY ${ALSA_UTILS_BUILD_DIR})

  execute_process(
    COMMAND ${CONFIGURE_SCRIPT}
      --prefix=${CMAKE_INSTALL_PREFIX}
      --disable-dependency-tracking
      --disable-silent-rules
    WORKING_DIRECTORY ${ALSA_UTILS_BUILD_DIR}
    RESULT_VARIABLE CONFIGURE_RESULT
    OUTPUT_VARIABLE CONFIGURE_OUTPUT
    ERROR_VARIABLE CONFIGURE_ERROR
  )
  if(CONFIGURE_RESULT EQUAL 0)
    message(STATUS "alsa-utils configure completed")
  else()
    message(FATAL_ERROR "alsa-utils configure failed (exit code ${CONFIGURE_RESULT})")
    message(STATUS "Output: ${CONFIGURE_OUTPUT}")
    message(STATUS "Error: ${CONFIGURE_ERROR}")
  endif()

  # Build alsatplg
  execute_process(
    COMMAND make -j${CMAKE_JOB_POOLS} alsatplg
    WORKING_DIRECTORY ${ALSA_UTILS_BUILD_DIR}
    RESULT_VARIABLE BUILD_RESULT
    OUTPUT_VARIABLE BUILD_OUTPUT
    ERROR_VARIABLE BUILD_ERROR
  )
  if(BUILD_RESULT EQUAL 0)
    message(STATUS "alsatplg build completed")
  else()
    message(FATAL_ERROR "alsatplg build failed (exit code ${BUILD_RESULT})")
    message(STATUS "Output: ${BUILD_OUTPUT}")
    message(STATUS "Error: ${BUILD_ERROR}")
  endif()

  # Find the built alsatplg executable
  find_program(ALSATPLG_EXECUTABLE alsatplg
    PATHS ${ALSA_UTILS_BUILD_DIR}/src ${CMAKE_BINARY_DIR}/alsa-utils-build/src
    NO_DEFAULT_PATH
  )

  if(ALSATPLG_EXECUTABLE)
    message(STATUS "alsatplg executable: ${ALSATPLG_EXECUTABLE}")

    # Copy to bin directory
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E copy ${ALSATPLG_EXECUTABLE} ${CMAKE_BINARY_DIR}/bin/alsatplg
      RESULT_VARIABLE COPY_RESULT
    )
    if(COPY_RESULT EQUAL 0)
      message(STATUS "alsatplg copied to ${CMAKE_BINARY_DIR}/bin/alsatplg")
    endif()

    # Create imported target
    add_executable(alsatplg IMPORTED GLOBAL)
    set_target_properties(alsatplg PROPERTIES IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/bin/alsatplg)
  else()
    message(FATAL_ERROR "alsatplg executable not found after build")
  endif()
endif()
