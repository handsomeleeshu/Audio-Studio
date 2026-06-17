# alsatplg - ALSA Topology Compiler
#
# This builds alsatplg by calling autotools commands (autogen.sh, configure, make)
# on the alsa-utils submodule. This ensures the build is identical to the upstream
# build process and avoids issues with missing generated files.

cmake_minimum_required(VERSION 3.16)
project(alsatplg NONE)

# Path to alsa-utils source
set(ALSA_UTILS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../alsa-utils")
set(ALSATPLG_SOURCE_DIR "${ALSA_UTILS_SOURCE_DIR}/topology")

# Validate source directory exists
if(NOT EXISTS "${ALSATPLG_SOURCE_DIR}/topology.c")
  message(FATAL_ERROR "alsatplg source not found at ${ALSATPLG_SOURCE_DIR}")
  message(FATAL_ERROR "Ensure alsa-utils submodule is initialized: git submodule update --init third_party/alsa-utils")
endif()

# Check if autogen.sh exists
set(AUTOGEN_SH "${ALSA_UTILS_SOURCE_DIR}/autogen.sh")
if(NOT EXISTS "${AUTOGEN_SH}")
  message(FATAL_ERROR "autogen.sh not found at ${AUTOGEN_SH}")
endif()

# Find required autotools tools
find_program(AUTOGEN_SH_SCRIPT autogen.sh NO_CMAKE_FIND_ROOT_PATH)
find_program(AUTOCONF_EXECUTABLE autoconf NO_CMAKE_FIND_ROOT_PATH)
find_program(AUTOMAKE_EXECUTABLE automake NO_CMAKE_FIND_ROOT_PATH)
find_program(LIBTOOLIZE_EXECUTABLE libtoolize NO_CMAKE_FIND_ROOT_PATH)

if(NOT AUTOGEN_SH_SCRIPT)
  set(AUTOGEN_SH_SCRIPT "${AUTOGEN_SH}")
endif()

# Check if we need to run autogen.sh
set(CONFIGURE_SCRIPT "${ALSA_UTILS_SOURCE_DIR}/configure")
if(NOT EXISTS "${CONFIGURE_SCRIPT}")
  set(NEED_AUTOGEN TRUE)
  message(STATUS "configure not found, will run autogen.sh")
else()
  set(NEED_AUTOGEN FALSE)
  message(STATUS "configure exists, skipping autogen.sh")
endif()

# Configure step
set(CONFIGURE_OPTIONS --disable-dependency-tracking --disable-silent-rules)

# Add alsa-lib from submodule if available
set(ALSA_LIB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../alsa-lib")
if(EXISTS "${ALSA_LIB_SOURCE_DIR}/configure.ac")
  message(STATUS "Using alsa-lib from submodule: ${ALSA_LIB_SOURCE_DIR}")
  # Build alsa-lib first
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}"
            ${AUTOGEN_SH_SCRIPT}
    WORKING_DIRECTORY ${ALSA_LIB_SOURCE_DIR}
    RESULT_VARIABLE AUTOMAKE_RESULT
  )
  if(AUTOMAKE_RESULT EQUAL 0)
    message(STATUS "alsa-lib autogen.sh completed")
  else()
    message(WARNING "alsa-lib autogen.sh failed (exit code ${AUTOMAKE_RESULT})")
  endif()

  set(ALSA_LIB_CONFIGURE_OPTIONS
    "--with-pic"
    "--disable-shared"
    "--enable-static"
    "--disable-python"
    "CFLAGS=-fPIC"
  )

  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
            "CC=${CMAKE_C_COMPILER}"
            "CXX=${CMAKE_CXX_COMPILER}"
            ${CONFIGURE_SCRIPT}
            --prefix=${CMAKE_INSTALL_PREFIX}
            ${ALSA_LIB_CONFIGURE_OPTIONS}
    WORKING_DIRECTORY ${ALSA_LIB_SOURCE_DIR}
    RESULT_VARIABLE CONFIGURE_RESULT
  )
  if(CONFIGURE_RESULT EQUAL 0)
    message(STATUS "alsa-lib configure completed")
  else()
    message(WARNING "alsa-lib configure failed (exit code ${CONFIGURE_RESULT})")
  endif()

  # Build and install alsa-lib
  execute_process(
    COMMAND make -j${CMAKE_JOB_POOLS}
    WORKING_DIRECTORY ${ALSA_LIB_SOURCE_DIR}
    RESULT_VARIABLE BUILD_RESULT
  )
  if(BUILD_RESULT EQUAL 0)
    message(STATUS "alsa-lib build completed")
  else()
    message(WARNING "alsa-lib build failed (exit code ${BUILD_RESULT})")
  endif()

  # Install alsa-lib to a temporary location
  set(ALSA_LIB_INSTALL_DIR "${CMAKE_BINARY_DIR}/alsa-lib")
  execute_process(
    COMMAND make install DESTDIR=${ALSA_LIB_INSTALL_DIR}
    WORKING_DIRECTORY ${ALSA_LIB_SOURCE_DIR}
    RESULT_VARIABLE INSTALL_RESULT
  )
  if(INSTALL_RESULT EQUAL 0)
    message(STATUS "alsa-lib installed to ${ALSA_LIB_INSTALL_DIR}")
  else()
    message(WARNING "alsa-lib install failed (exit code ${INSTALL_RESULT})")
  endif()

  # Add alsa-lib include and lib paths to alsa-utils configure
  list(APPEND CONFIGURE_OPTIONS
    "CPPFLAGS=-I${ALSA_LIB_INSTALL_DIR}${CMAKE_INSTALL_PREFIX}/include"
    "LDFLAGS=-L${ALSA_LIB_INSTALL_DIR}${CMAKE_INSTALL_PREFIX}/lib"
  )
endif()

# Run autogen.sh if needed
if(NEED_AUTOGEN)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "NOCONFIGURE=1"
            ${AUTOGEN_SH_SCRIPT}
    WORKING_DIRECTORY ${ALSA_UTILS_SOURCE_DIR}
    RESULT_VARIABLE AUTOMAKE_RESULT
  )
  if(AUTOMAKE_RESULT EQUAL 0)
    message(STATUS "autogen.sh completed")
  else()
    message(FATAL_ERROR "autogen.sh failed (exit code ${AUTOMAKE_RESULT})")
  endif()
endif()

# Configure alsa-utils
execute_process(
  COMMAND ${CONFIGURE_SCRIPT}
    --prefix=${CMAKE_INSTALL_PREFIX}
    ${CONFIGURE_OPTIONS}
  WORKING_DIRECTORY ${ALSA_UTILS_SOURCE_DIR}
  RESULT_VARIABLE CONFIGURE_RESULT
)
if(CONFIGURE_RESULT EQUAL 0)
  message(STATUS "alsa-utils configure completed")
else()
  message(FATAL_ERROR "alsa-utils configure failed (exit code ${CONFIGURE_RESULT})")
endif()

# Build alsatplg
execute_process(
  COMMAND make -j${CMAKE_JOB_POOLS} alsatplg
  WORKING_DIRECTORY ${ALSA_UTILS_SOURCE_DIR}
  RESULT_VARIABLE BUILD_RESULT
)
if(BUILD_RESULT EQUAL 0)
  message(STATUS "alsatplg build completed")
else()
  message(FATAL_ERROR "alsatplg build failed (exit code ${BUILD_RESULT})")
endif()

# Install alsatplg
execute_process(
  COMMAND make install alsatplg DESTDIR=${CMAKE_INSTALL_PREFIX}
  WORKING_DIRECTORY ${ALSA_UTILS_SOURCE_DIR}
  RESULT_VARIABLE INSTALL_RESULT
)
if(INSTALL_RESULT EQUAL 0)
  message(STATUS "alsatplg installed to ${CMAKE_INSTALL_PREFIX}")
else()
  message(FATAL_ERROR "alsatplg install failed (exit code ${INSTALL_RESULT})")
endif()

# Find the installed alsatplg executable
find_program(ALSATPLG_EXECUTABLE alsatplg
  PATHS ${CMAKE_INSTALL_PREFIX}/bin ${CMAKE_INSTALL_PREFIX}/local/bin
  NO_DEFAULT_PATH
)

if(ALSATPLG_EXECUTABLE)
  message(STATUS "alsatplg executable: ${ALSATPLG_EXECUTABLE}")

  # Create an imported target for alsatplg
  add_executable(alsatplg IMPORTED GLOBAL)
  set_target_properties(alsatplg PROPERTIES
    IMPORTED_LOCATION ${ALSATPLG_EXECUTABLE}
  )
endif()
