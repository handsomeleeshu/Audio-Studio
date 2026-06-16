# Copyright (c) 2023, VeriSilicon Holdings Co., Ltd. All rights reserved
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors
# may be used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Looks for defconfig files in arch directories where kconfig.cmake looks too.
set(DEFCONFIGS_DIRECTORY "${PROJECT_SOURCE_DIR}/configs")
file(GLOB_RECURSE DEFCONFIG_PATHS "${DEFCONFIGS_DIRECTORY}/*_defconfig")

# Adds dependency on defconfigs directory
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DEFCONFIGS_DIRECTORY})

# Adds target for every defconfig, so you we can use it like make *_defconfig
foreach(defconfig_path ${DEFCONFIG_PATHS})
	get_filename_component(defconfig_name ${defconfig_path} NAME)
	add_custom_target(
		${defconfig_name}
		COMMAND ${CMAKE_COMMAND} -E copy
			${defconfig_path}
			${DOT_CONFIG_PATH}
		COMMAND ${CMAKE_COMMAND} -E env
			srctree=${PROJECT_SOURCE_DIR}
			CC_VERSION_TEXT=${CC_VERSION_TEXT}
			${PYTHON3} ${PROJECT_SOURCE_DIR}/scripts/kconfig/olddefconfig.py
			${PROJECT_SOURCE_DIR}/Kconfig
		WORKING_DIRECTORY ${GENERATED_DIRECTORY}
		COMMENT "Applying olddefconfig with ${defconfig_name}"
		VERBATIM
		USES_TERMINAL
	)
endforeach()

set(OVERRIDE_DEFCONFIGS_DIRECTORY "${DEFCONFIGS_DIRECTORY}/override")
file(GLOB OVERRIDE_DEFCONFIGS_PATHS "${OVERRIDE_DEFCONFIGS_DIRECTORY}/*.config")

foreach(config_path ${OVERRIDE_DEFCONFIGS_PATHS})
	get_filename_component(config_name ${config_path} NAME_WE)
	add_custom_target(
		"${config_name}_overridedefconfig"
		COMMAND ${CMAKE_COMMAND} -E copy
			${config_path}
			${PROJECT_BINARY_DIR}/override.config
		COMMAND ${CMAKE_COMMAND} -E env
			srctree=${PROJECT_SOURCE_DIR}
			CC_VERSION_TEXT=${CC_VERSION_TEXT}
			${PYTHON3} ${PROJECT_SOURCE_DIR}/scripts/kconfig/overrideconfig.py
			${PROJECT_SOURCE_DIR}/Kconfig
			${PROJECT_BINARY_DIR}/override.config
		WORKING_DIRECTORY ${GENERATED_DIRECTORY}
		COMMENT "Applying overrideconfig with ${config_name}"
		VERBATIM
		USES_TERMINAL
	)
endforeach()
