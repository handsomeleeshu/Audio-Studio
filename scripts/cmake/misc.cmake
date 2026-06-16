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

# Reads configs from kconfig file and set them as cmake variables.
# Each config is in format CONFIG_<NAME>=<VALUE>.
# Configs are added to parent scope with CONFIG_ prefix (as written in file).
function(read_kconfig_config config_file)
	file(
		STRINGS
		${config_file}
		configs_list
		REGEX "^CONFIG_"
		ENCODING "UTF-8"
	)

	foreach(config ${configs_list})
		string(REGEX MATCH "^([^=]+)=(.*)$" ignored ${config})
		set(config_name ${CMAKE_MATCH_1})
		set(config_value ${CMAKE_MATCH_2})

		if("${config_value}" MATCHES "^\"(.*)\"$")
			set(config_value ${CMAKE_MATCH_1})
		endif()

		set("${config_name}" "${config_value}" PARENT_SCOPE)
	endforeach()
endfunction()

# create optimization flags based on cmake variables set from Kconfig
function(get_optimization_flag OUT_VAR)
	if(CONFIG_OPTIMIZE_FOR_PERFORMANCE)
		set(${OUT_VAR} "-O2" PARENT_SCOPE)
	elseif(CONFIG_OPTIMIZE_FOR_SIZE)
		set(${OUT_VAR} "-O2 -Os" PARENT_SCOPE)
	elseif(CONFIG_OPTIMIZE_FOR_DEBUG)
		set(${OUT_VAR} "-O0" PARENT_SCOPE)
	else()
		message(FATAL_ERROR "no CONFIG_OPTIMIZE_ found")
	endif()
endfunction()

# create optimization flags based on cmake variables set from Kconfig
function(get_debug_flag OUT_VAR)
	if(CONFIG_OPTIMIZE_FOR_PERFORMANCE)
		set(${OUT_VAR} "" PARENT_SCOPE)
	elseif(CONFIG_OPTIMIZE_FOR_SIZE)
		set(${OUT_VAR} "" PARENT_SCOPE)
	elseif(CONFIG_OPTIMIZE_FOR_DEBUG)
		set(${OUT_VAR} "-g" PARENT_SCOPE)
	else()
		message(FATAL_ERROR "no CONFIG_OPTIMIZE_ found")
	endif()
endfunction()

# Adds sources to target like target_sources, but assumes that
# paths are relative to subdirectory.
# Works like:
# 	Cmake >= 3.13:
#		target_sources(<target> PRIVATE <sources>)
# 	Cmake < 3.13:
#		target_sources(<target> PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/<sources>)
function(add_local_sources target)
	foreach(arg ${ARGN})
		if(IS_ABSOLUTE ${arg})
			set(path ${arg})
		else()
			set(path ${CMAKE_CURRENT_SOURCE_DIR}/${arg})
		endif()

		target_sources(${target} PRIVATE ${path})
	# -imacros${CONFIG_H_PATH} escapes regular .h dep scanning
	#	add_dependencies(${target} genconfig) # has no effect?
		set_source_files_properties(${path}
			PROPERTIES
			OBJECT_DEPENDS ${CONFIG_H_PATH}
		)
	endforeach()
endfunction()
