function(audio_studio_require_sources target sources)
  if(NOT sources)
    message(FATAL_ERROR "${target} is enabled but no implementation source was selected by CONFIG")
  endif()
endfunction()

if(CONFIG_DRIVER_DUMMY)
  add_subdirectory(dummy)
endif()

if(CONFIG_DRIVER_OS)
  add_subdirectory(os)
endif()

if(CONFIG_DRIVER_SOCKET)
  add_subdirectory(socket)
endif()

if(CONFIG_DRIVER_FILESYSTEM)
  add_subdirectory(filesystem)
endif()

if(CONFIG_DRIVER_PIPE)
  add_subdirectory(pipe)
endif()

if(CONFIG_DRIVER_DYNLIB)
  add_subdirectory(dynlib)
endif()

if(CONFIG_DRIVER_TRANSPORT)
  add_subdirectory(transport)
endif()

if(CONFIG_DRIVER_AUDIO)
  add_subdirectory(audio)
endif()

if(CONFIG_DRIVER_CONTROL)
  add_subdirectory(control)
endif()

if(CONFIG_DRIVER_LOG)
  add_subdirectory(log)
endif()

if(CONFIG_DRIVER_DUMP)
  add_subdirectory(dump)
endif()

if(CONFIG_DRIVERS_CORE)
  add_library(audio_studio_drivers_core
    src/driver_manager.cpp
  )
  target_include_directories(audio_studio_drivers_core PUBLIC
    include
    audio/include
    control/include
    dump/include
    dynlib/include
    filesystem/include
    log/include
    os/include
    pipe/include
    socket/include
    transport/include
  )
  target_link_libraries(audio_studio_drivers_core PUBLIC
    audio_studio_config
    audio_studio_framework_common
  )
  target_compile_features(audio_studio_drivers_core PUBLIC cxx_std_17)
endif()
