cmake_minimum_required(VERSION 3.16)

add_library(audio_studio_cli_common
  common/src/cli_common.cpp
)
target_include_directories(audio_studio_cli_common PUBLIC common/include)
target_link_libraries(audio_studio_cli_common PUBLIC audio_studio_config audio_studio_cli11 audio_studio_dummy_driver)
if(CONFIG_RPC_CLIENT)
  target_link_libraries(audio_studio_cli_common PUBLIC audio_studio_rpc_client)
  if(CONFIG_RPC_TRANSPORT_SOCKET)
    target_link_libraries(audio_studio_cli_common PUBLIC audio_studio_driver_socket)
  endif()
  if(CONFIG_RPC_TRANSPORT_PIPE)
    target_link_libraries(audio_studio_cli_common PUBLIC audio_studio_driver_pipe)
  endif()
endif()
target_compile_features(audio_studio_cli_common PUBLIC cxx_std_17)

foreach(tool as_control as_play as_record as_log as_dump)
  add_executable(${tool} tools/${tool}.cpp)
  target_link_libraries(${tool} PRIVATE audio_studio_cli_common)
  if(CONFIG_RPC_CLIENT AND CONFIG_RPC_TRANSPORT_SOCKET)
    target_link_libraries(${tool} PRIVATE audio_studio_driver_socket)
  endif()
  if(CONFIG_RPC_CLIENT AND CONFIG_RPC_TRANSPORT_PIPE)
    target_link_libraries(${tool} PRIVATE audio_studio_driver_pipe)
  endif()
  target_compile_features(${tool} PRIVATE cxx_std_17)
endforeach()

if(CONFIG_FRAMEWORK_CONFIG)
  add_executable(as_config tools/as_config.cpp)
  target_link_libraries(as_config PRIVATE
    audio_studio_cli_common
    audio_studio_framework_config
    audio_studio_drivers_core
    audio_studio_driver_os
    audio_studio_driver_filesystem
    audio_studio_driver_dynlib
  )
  target_compile_features(as_config PRIVATE cxx_std_17)
  if(TARGET audio_studio_builtin_module_configs)
    add_dependencies(as_config audio_studio_builtin_module_configs)
  endif()
endif()

if(CONFIG_BUILD_TESTS)
  add_executable(audio_studio_cli_tests tests/cli_tests.cpp)
  target_link_libraries(audio_studio_cli_tests PRIVATE audio_studio_cli_common audio_studio_dummy_driver)
  if(CONFIG_RPC_CLIENT AND CONFIG_RPC_TRANSPORT_SOCKET)
    target_link_libraries(audio_studio_cli_tests PRIVATE audio_studio_driver_socket)
  endif()
  if(CONFIG_RPC_CLIENT AND CONFIG_RPC_TRANSPORT_PIPE)
    target_link_libraries(audio_studio_cli_tests PRIVATE audio_studio_driver_pipe)
  endif()
  target_compile_features(audio_studio_cli_tests PRIVATE cxx_std_17)
  add_test(NAME cli_tests COMMAND audio_studio_cli_tests)
endif()
