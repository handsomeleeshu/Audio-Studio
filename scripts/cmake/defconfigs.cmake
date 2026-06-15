# SPDX-License-Identifier: BSD-3-Clause

set(DEFCONFIGS_DIRECTORY "${PROJECT_SOURCE_DIR}/src/arch/${ARCH}/configs")

if(EXISTS ${DEFCONFIGS_DIRECTORY})
  file(GLOB DEFCONFIG_PATHS "${DEFCONFIGS_DIRECTORY}/*_defconfig")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DEFCONFIGS_DIRECTORY})

  foreach(defconfig_path ${DEFCONFIG_PATHS})
    get_filename_component(defconfig_name ${defconfig_path} NAME)
    add_custom_target(
      ${defconfig_name}
      COMMAND ${CMAKE_COMMAND} -E copy ${defconfig_path} ${DOT_CONFIG_PATH}
      COMMAND ${CMAKE_COMMAND} -E env srctree=${PROJECT_SOURCE_DIR} ARCH=${ARCH} KCONFIG_CONFIG=${DOT_CONFIG_PATH} ${PYTHON3} ${PROJECT_SOURCE_DIR}/scripts/kconfig/olddefconfig.py ${PROJECT_SOURCE_DIR}/Kconfig
      WORKING_DIRECTORY ${GENERATED_DIRECTORY}
      COMMENT "Applying olddefconfig with ${defconfig_name}"
      VERBATIM
      USES_TERMINAL
    )
  endforeach()
endif()
