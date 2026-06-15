# SPDX-License-Identifier: BSD-3-Clause

function(read_kconfig_config config_file)
  file(STRINGS ${config_file} configs_list REGEX "^CONFIG_" ENCODING "UTF-8")
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
