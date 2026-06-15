# SPDX-License-Identifier: Apache-2.0

function(audio_studio_load_platform_config)
  if(NOT DEFINED PLATFORM)
    set(PLATFORM "simulator_linux" CACHE STRING "Audio Studio platform")
  endif()

  set(PLATFORM_DIR "${CMAKE_CURRENT_LIST_DIR}/../../platform/${PLATFORM}")
  set(PLATFORM_CONFIG "${PLATFORM_DIR}/config.ini")
  if(NOT EXISTS "${PLATFORM_CONFIG}")
    message(FATAL_ERROR "Unknown Audio Studio platform: ${PLATFORM}")
  endif()

  file(STRINGS "${PLATFORM_CONFIG}" _platform_lines)
  foreach(_line IN LISTS _platform_lines)
    if(_line MATCHES "^default_config=(.*)$")
      set(INIT_CONFIG "${CMAKE_CURRENT_LIST_DIR}/../../${CMAKE_MATCH_1}" CACHE FILEPATH "Initial Kconfig input" FORCE)
    elseif(_line MATCHES "^toolchain=(.*)$")
      set(AUDIO_STUDIO_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../../${CMAKE_MATCH_1}" CACHE FILEPATH "Platform toolchain" FORCE)
    endif()
  endforeach()

  if(NOT EXISTS "${INIT_CONFIG}")
    message(FATAL_ERROR "Missing platform default config: ${INIT_CONFIG}")
  endif()

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
  set(DOT_CONFIG_PATH "${GENERATED_DIR}/.config")
  set(CONFIG_H_PATH "${GENERATED_DIR}/include/generated/autoconf.h")
  set(CONFIG_CMAKE_PATH "${GENERATED_DIR}/autoconf.cmake")

  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../kconfig/genconfig.py"
            --kconfig "${CMAKE_CURRENT_LIST_DIR}/../../Kconfig"
            --config "${INIT_CONFIG}"
            --dotconfig "${DOT_CONFIG_PATH}"
            --header "${CONFIG_H_PATH}"
            --cmake "${CONFIG_CMAKE_PATH}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    RESULT_VARIABLE _kconfig_result
  )
  if(NOT _kconfig_result EQUAL 0)
    message(FATAL_ERROR "Kconfig generation failed")
  endif()

  include("${CONFIG_CMAKE_PATH}")
  include_directories("${GENERATED_DIR}/include")
endfunction()
