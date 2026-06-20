cmake_minimum_required(VERSION 3.16)

if(CONFIG_FRAMEWORK_COMMON)
  add_library(audio_studio_framework_common
    framework/common/src/service_registry.cpp
    framework/common/src/status.cpp
  )
  target_include_directories(audio_studio_framework_common PUBLIC framework/common/include)
  target_compile_features(audio_studio_framework_common PUBLIC cxx_std_17)
endif()

if(CONFIG_DRIVERS_CORE OR CONFIG_DRIVER_DUMMY OR CONFIG_DRIVER_OS OR CONFIG_DRIVER_SOCKET OR
   CONFIG_DRIVER_FILESYSTEM OR CONFIG_DRIVER_PIPE OR CONFIG_DRIVER_DYNLIB OR
   CONFIG_DRIVER_TRANSPORT OR CONFIG_DRIVER_AUDIO OR CONFIG_DRIVER_CONTROL OR
   CONFIG_DRIVER_LOG OR CONFIG_DRIVER_DUMP)
  add_subdirectory("${PROJECT_SOURCE_DIR}/drivers" "${CMAKE_BINARY_DIR}/drivers")
endif()

if(CONFIG_PLATFORM_CORE)
  add_library(audio_studio_platform_core
    platform/core/src/platform_registry.cpp
  )
  target_include_directories(audio_studio_platform_core PUBLIC platform/core/include)
  target_link_libraries(audio_studio_platform_core PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_platform_core PUBLIC cxx_std_17)
endif()

if(CONFIG_PLATFORM_A2)
  add_library(audio_studio_platform_a2
    platform/a2/src/a2_platform.cpp
  )
  target_include_directories(audio_studio_platform_a2 PUBLIC platform/a2/include)
  target_link_libraries(audio_studio_platform_a2 PUBLIC audio_studio_platform_core)
  target_compile_features(audio_studio_platform_a2 PUBLIC cxx_std_17)
endif()

if(CONFIG_PLATFORM_SIMULATOR)
  add_library(audio_studio_platform_simulator OBJECT
    platform/simulator/src/rv32qemu_log_device.cpp
    platform/simulator/src/simulator_pipe_transport_driver.cpp
    platform/simulator/src/simulator_platform.cpp
  )
  target_include_directories(audio_studio_platform_simulator PUBLIC platform/simulator/include)
  target_link_libraries(audio_studio_platform_simulator PUBLIC
    audio_studio_platform_core
    audio_studio_driver_log
    audio_studio_driver_transport
    audio_studio_framework_transport)
  target_compile_features(audio_studio_platform_simulator PUBLIC cxx_std_17)
endif()

if(CONFIG_FRAMEWORK_SESSION)
  add_library(audio_studio_framework_session
    framework/session/src/session_registry.cpp
  )
  target_include_directories(audio_studio_framework_session PUBLIC framework/session/include)
  target_link_libraries(audio_studio_framework_session PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_framework_session PUBLIC cxx_std_17)
endif()

if(CONFIG_FRAMEWORK_CONTROL)
  add_library(audio_studio_framework_control
    framework/control/src/control_service.cpp
  )
  target_include_directories(audio_studio_framework_control PUBLIC framework/control/include)
  target_link_libraries(audio_studio_framework_control PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_framework_control PUBLIC cxx_std_17)
endif()

if(CONFIG_FRAMEWORK_AUDIO)
  add_library(audio_studio_framework_audio
    framework/audio/src/audio_service.cpp
  )
  target_include_directories(audio_studio_framework_audio PUBLIC
    framework/audio/include
    "${PROJECT_SOURCE_DIR}/drivers/audio/include"
  )
  target_link_libraries(audio_studio_framework_audio PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_framework_audio PUBLIC cxx_std_17)
endif()

if(CONFIG_FRAMEWORK_CONFIG)
  add_library(audio_studio_framework_config
    framework/config/src/config_service.cpp
  )
  target_include_directories(audio_studio_framework_config PUBLIC
    framework/config/include
    "${PROJECT_SOURCE_DIR}/drivers/filesystem/include"
    "${PROJECT_SOURCE_DIR}/drivers/os/include"
    "${PROJECT_SOURCE_DIR}/drivers/dynlib/include"
  )
  target_link_libraries(audio_studio_framework_config PUBLIC
    audio_studio_framework_common
    audio_studio_json
    AudioStudio::ModuleConfigSdk
  )
  target_compile_features(audio_studio_framework_config PUBLIC cxx_std_17)
endif()

if(CONFIG_FRAMEWORK_LOG)
  add_library(audio_studio_framework_log
    framework/log/src/log_service.cpp
  )
  target_include_directories(audio_studio_framework_log PUBLIC
    framework/log/include
    "${PROJECT_SOURCE_DIR}/drivers/log/include")
  target_link_libraries(audio_studio_framework_log PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_framework_log PUBLIC cxx_std_17)
endif()

if(CONFIG_FRAMEWORK_DUMP)
  add_library(audio_studio_framework_dump
    framework/dump/src/dump_service.cpp
  )
  target_include_directories(audio_studio_framework_dump PUBLIC framework/dump/include)
  target_link_libraries(audio_studio_framework_dump PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_framework_dump PUBLIC cxx_std_17)
endif()

if(CONFIG_FRAMEWORK_PLUGIN)
  add_library(audio_studio_framework_plugin
    framework/plugin/src/plugin_manager.cpp
  )
  target_include_directories(audio_studio_framework_plugin PUBLIC framework/plugin/include)
  target_link_libraries(audio_studio_framework_plugin PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_framework_plugin PUBLIC cxx_std_17)
endif()

if(CONFIG_FRAMEWORK_TRANSPORT)
  add_library(audio_studio_framework_transport
    framework/transport/src/datalink_frame.cpp
    framework/transport/src/datalink_manager.cpp
    framework/transport/src/frame_codec.cpp
    framework/transport/src/transport_manager.cpp
  )
  target_include_directories(audio_studio_framework_transport PUBLIC
    framework/transport/include
    "${PROJECT_SOURCE_DIR}/drivers/transport/include")
  target_link_libraries(audio_studio_framework_transport PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_framework_transport PUBLIC cxx_std_17)
endif()

if(CONFIG_RPC)
  add_subdirectory("${PROJECT_SOURCE_DIR}/rpc" "${CMAKE_BINARY_DIR}/rpc")
endif()

set(AUDIO_STUDIO_SERVER_LIBS)
if(CONFIG_FRAMEWORK_COMMON)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_common)
endif()
if(CONFIG_DRIVER_DUMMY)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_dummy_driver)
endif()
if(CONFIG_DRIVER_OS)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_driver_os)
endif()
if(CONFIG_DRIVER_SOCKET)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_driver_socket)
endif()
if(CONFIG_DRIVER_FILESYSTEM)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_driver_filesystem)
endif()
if(CONFIG_DRIVER_PIPE)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_driver_pipe)
endif()
if(CONFIG_DRIVER_DYNLIB)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_driver_dynlib)
endif()
if(CONFIG_DRIVER_TRANSPORT)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_driver_transport)
endif()
if(CONFIG_DRIVER_AUDIO)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_driver_audio)
endif()
if(CONFIG_DRIVER_CONTROL)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_driver_control)
endif()
if(CONFIG_DRIVER_LOG)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_driver_log)
endif()
if(CONFIG_DRIVER_DUMP)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_driver_dump)
endif()
if(CONFIG_DRIVERS_CORE)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_drivers_core)
endif()
if(CONFIG_PLATFORM_CORE)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_platform_core)
endif()
if(CONFIG_PLATFORM_A2)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_platform_a2)
endif()
if(CONFIG_PLATFORM_SIMULATOR)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_platform_simulator)
endif()
if(CONFIG_FRAMEWORK_SESSION)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_session)
endif()
if(CONFIG_FRAMEWORK_CONTROL)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_control)
endif()
if(CONFIG_FRAMEWORK_AUDIO)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_audio)
endif()
if(CONFIG_FRAMEWORK_CONFIG)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_config)
endif()
if(CONFIG_FRAMEWORK_LOG)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_log)
endif()
if(CONFIG_FRAMEWORK_DUMP)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_dump)
endif()
if(CONFIG_FRAMEWORK_PLUGIN)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_plugin)
endif()
if(CONFIG_FRAMEWORK_TRANSPORT)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_transport)
endif()
if(CONFIG_RPC_CORE)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_rpc_core)
endif()
if(CONFIG_RPC_AUDIO_METHODS)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_rpc_audio)
endif()
if(CONFIG_RPC_SERVER)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_rpc_server)
endif()
if(CONFIG_RPC_CLIENT)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_rpc_client)
endif()

add_executable(as_server as_server/main.cpp)
target_link_libraries(as_server PRIVATE audio_studio_config audio_studio_cli11 ${AUDIO_STUDIO_SERVER_LIBS})
target_compile_features(as_server PRIVATE cxx_std_17)
if(CONFIG_FRAMEWORK_CONFIG AND TARGET audio_studio_builtin_module_configs)
  add_dependencies(as_server audio_studio_builtin_module_configs)
endif()

if(CONFIG_BUILD_TESTS AND CONFIG_SERVER_FRAMEWORK_TESTS)
  add_executable(audio_studio_server_tests
    tests/server_tests.cpp
  )
  target_link_libraries(audio_studio_server_tests PRIVATE ${AUDIO_STUDIO_SERVER_LIBS})
  target_compile_definitions(audio_studio_server_tests PRIVATE
    AUDIO_STUDIO_TEST_ROOT="${PROJECT_SOURCE_DIR}"
  )
  if(CONFIG_FRAMEWORK_CONFIG)
    add_library(audio_studio_config_test_plugin SHARED
      tests/config_test_plugin.cpp
    )
    target_link_libraries(audio_studio_config_test_plugin PRIVATE AudioStudio::ModuleConfigSdk)
    target_compile_features(audio_studio_config_test_plugin PRIVATE cxx_std_17)
    target_compile_definitions(audio_studio_server_tests PRIVATE
      AUDIO_STUDIO_CONFIG_TEST_PLUGIN_PATH="$<TARGET_FILE:audio_studio_config_test_plugin>"
    )
    add_dependencies(audio_studio_server_tests audio_studio_config_test_plugin)
  endif()
  if(CONFIG_FRAMEWORK_CONFIG AND TARGET audio_studio_builtin_module_configs)
    add_dependencies(audio_studio_server_tests audio_studio_builtin_module_configs)
  endif()
  target_compile_features(audio_studio_server_tests PRIVATE cxx_std_17)
  add_test(NAME server_framework_tests COMMAND audio_studio_server_tests)
endif()

if(CONFIG_BUILD_TESTS AND CONFIG_DRIVER_INTERFACE_TESTS)
  add_library(audio_studio_driver_test_plugin SHARED
    tests/driver_test_plugin.cpp
  )
  target_compile_features(audio_studio_driver_test_plugin PRIVATE cxx_std_17)

  add_executable(audio_studio_driver_interface_tests
    tests/driver_interface_tests.cpp
  )
  target_link_libraries(audio_studio_driver_interface_tests PRIVATE ${AUDIO_STUDIO_SERVER_LIBS})
  target_compile_definitions(audio_studio_driver_interface_tests PRIVATE
    AUDIO_STUDIO_TEST_PLUGIN_PATH=\"$<TARGET_FILE:audio_studio_driver_test_plugin>\"
  )
  add_dependencies(audio_studio_driver_interface_tests audio_studio_driver_test_plugin)
  target_compile_features(audio_studio_driver_interface_tests PRIVATE cxx_std_17)
  add_test(NAME driver_interface_tests COMMAND audio_studio_driver_interface_tests)
endif()
