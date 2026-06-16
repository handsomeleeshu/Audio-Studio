cmake_minimum_required(VERSION 3.16)

if(CONFIG_RPC_CORE)
  add_library(audio_studio_rpc_core
    src/json_value.cpp
    src/json_rpc.cpp
    src/rpc_framing.cpp
    src/rpc_api_registry.cpp
  )
  target_include_directories(audio_studio_rpc_core PUBLIC include)
  target_compile_features(audio_studio_rpc_core PUBLIC cxx_std_17)
endif()

if(CONFIG_RPC_AUDIO_METHODS)
  add_library(audio_studio_rpc_audio
    api/audio_studio_rpc_api.cpp
    src/audio_rpc.cpp
    src/rpc_runtime_context.cpp
  )
  target_include_directories(audio_studio_rpc_audio PUBLIC include)
  target_link_libraries(audio_studio_rpc_audio PUBLIC
    audio_studio_rpc_core
    audio_studio_config
    audio_studio_framework_audio
  )
  target_compile_features(audio_studio_rpc_audio PUBLIC cxx_std_17)
endif()

if(CONFIG_RPC_CLIENT)
  set(AUDIO_STUDIO_RPC_CLIENT_SOURCES)
  if(CONFIG_RPC_TRANSPORT_SOCKET)
    list(APPEND AUDIO_STUDIO_RPC_CLIENT_SOURCES src/rpc_socket_transport.cpp)
  endif()
  if(CONFIG_RPC_TRANSPORT_PIPE)
    list(APPEND AUDIO_STUDIO_RPC_CLIENT_SOURCES src/rpc_pipe_transport.cpp)
  endif()
  if(NOT AUDIO_STUDIO_RPC_CLIENT_SOURCES)
    message(FATAL_ERROR "CONFIG_RPC_CLIENT requires at least one RPC transport")
  endif()

  add_library(audio_studio_rpc_client
    src/audio_rpc_client.cpp
    ${AUDIO_STUDIO_RPC_CLIENT_SOURCES}
  )
  target_include_directories(audio_studio_rpc_client PUBLIC
    include
    "${PROJECT_SOURCE_DIR}/drivers/include"
    "${PROJECT_SOURCE_DIR}/drivers/socket/include"
    "${PROJECT_SOURCE_DIR}/drivers/pipe/include"
  )
  target_link_libraries(audio_studio_rpc_client PUBLIC
    audio_studio_rpc_core
    audio_studio_framework_common
    audio_studio_drivers_core
  )
  if(CONFIG_RPC_AUDIO_METHODS)
    target_link_libraries(audio_studio_rpc_client PUBLIC audio_studio_rpc_audio)
  endif()
  if(CONFIG_RPC_TRANSPORT_SOCKET)
    target_link_libraries(audio_studio_rpc_client PUBLIC audio_studio_driver_socket)
  endif()
  if(CONFIG_RPC_TRANSPORT_PIPE)
    target_link_libraries(audio_studio_rpc_client PUBLIC audio_studio_driver_pipe)
  endif()
  target_compile_features(audio_studio_rpc_client PUBLIC cxx_std_17)
endif()

if(CONFIG_RPC_SERVER)
  set(AUDIO_STUDIO_RPC_SERVER_SOURCES src/rpc_server.cpp)
  if(CONFIG_RPC_TRANSPORT_SOCKET)
    list(APPEND AUDIO_STUDIO_RPC_SERVER_SOURCES src/rpc_socket_server.cpp)
  endif()
  if(CONFIG_RPC_TRANSPORT_PIPE)
    list(APPEND AUDIO_STUDIO_RPC_SERVER_SOURCES src/rpc_pipe_server.cpp)
  endif()

  add_library(audio_studio_rpc_server
    ${AUDIO_STUDIO_RPC_SERVER_SOURCES}
  )
  target_include_directories(audio_studio_rpc_server PUBLIC
    include
    "${PROJECT_SOURCE_DIR}/drivers/include"
    "${PROJECT_SOURCE_DIR}/drivers/socket/include"
    "${PROJECT_SOURCE_DIR}/drivers/pipe/include"
  )
  target_link_libraries(audio_studio_rpc_server PUBLIC
    audio_studio_rpc_core
    audio_studio_framework_common
    audio_studio_drivers_core
  )
  if(CONFIG_RPC_AUDIO_METHODS)
    target_link_libraries(audio_studio_rpc_server PUBLIC audio_studio_rpc_audio)
  endif()
  if(CONFIG_RPC_TRANSPORT_SOCKET)
    target_link_libraries(audio_studio_rpc_server PUBLIC audio_studio_driver_socket)
  endif()
  if(CONFIG_RPC_TRANSPORT_PIPE)
    target_link_libraries(audio_studio_rpc_server PUBLIC audio_studio_driver_pipe)
  endif()
  target_compile_features(audio_studio_rpc_server PUBLIC cxx_std_17)
endif()
