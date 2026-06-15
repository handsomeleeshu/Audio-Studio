cmake_minimum_required(VERSION 3.16)

if(CONFIG_FRAMEWORK_COMMON)
  add_library(audio_studio_framework_common
    framework/common/src/service_registry.cpp
    framework/common/src/status.cpp
  )
  target_include_directories(audio_studio_framework_common PUBLIC framework/common/include)
  target_compile_features(audio_studio_framework_common PUBLIC cxx_std_17)
endif()

add_subdirectory(drivers)

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
  target_include_directories(audio_studio_framework_audio PUBLIC framework/audio/include)
  target_link_libraries(audio_studio_framework_audio PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_framework_audio PUBLIC cxx_std_17)
endif()

if(CONFIG_FRAMEWORK_LOG)
  add_library(audio_studio_framework_log
    framework/log/src/log_service.cpp
  )
  target_include_directories(audio_studio_framework_log PUBLIC framework/log/include)
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
    framework/transport/src/frame_codec.cpp
    framework/transport/src/transport_manager.cpp
  )
  target_include_directories(audio_studio_framework_transport PUBLIC framework/transport/include)
  target_link_libraries(audio_studio_framework_transport PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_framework_transport PUBLIC cxx_std_17)
endif()

if(CONFIG_RPC)
  add_library(audio_studio_rpc
    rpc/src/json_rpc.cpp
  )
  target_include_directories(audio_studio_rpc PUBLIC rpc/include)
  target_link_libraries(audio_studio_rpc PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_rpc PUBLIC cxx_std_17)
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
if(CONFIG_FRAMEWORK_SESSION)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_session)
endif()
if(CONFIG_FRAMEWORK_CONTROL)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_control)
endif()
if(CONFIG_FRAMEWORK_AUDIO)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_framework_audio)
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
if(CONFIG_RPC)
  list(APPEND AUDIO_STUDIO_SERVER_LIBS audio_studio_rpc)
endif()

add_executable(as_server as_server/main.cpp)
target_link_libraries(as_server PRIVATE audio_studio_config ${AUDIO_STUDIO_SERVER_LIBS})
target_compile_features(as_server PRIVATE cxx_std_17)

if(CONFIG_BUILD_TESTS AND CONFIG_SERVER_FRAMEWORK_TESTS)
  add_executable(audio_studio_server_tests
    tests/server_tests.cpp
  )
  target_link_libraries(audio_studio_server_tests PRIVATE ${AUDIO_STUDIO_SERVER_LIBS})
  target_compile_features(audio_studio_server_tests PRIVATE cxx_std_17)
  add_test(NAME server_framework_tests COMMAND audio_studio_server_tests)
endif()

if(CONFIG_BUILD_TESTS AND CONFIG_DRIVER_INTERFACE_TESTS)
  add_executable(audio_studio_driver_interface_tests
    tests/driver_interface_tests.cpp
  )
  target_link_libraries(audio_studio_driver_interface_tests PRIVATE ${AUDIO_STUDIO_SERVER_LIBS})
  target_compile_features(audio_studio_driver_interface_tests PRIVATE cxx_std_17)
  add_test(NAME driver_interface_tests COMMAND audio_studio_driver_interface_tests)
endif()
