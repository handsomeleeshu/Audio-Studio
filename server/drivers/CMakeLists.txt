function(audio_studio_require_sources target sources)
  if(NOT sources)
    message(FATAL_ERROR "${target} is enabled but no implementation source was selected by CONFIG")
  endif()
endfunction()

if(CONFIG_DRIVER_DUMMY)
  add_subdirectory(dummy/src)
endif()

if(CONFIG_DRIVER_OS)
  add_subdirectory(os/src)
endif()

if(CONFIG_DRIVER_SOCKET)
  add_subdirectory(socket/src)
endif()

if(CONFIG_DRIVER_FILESYSTEM)
  add_subdirectory(filesystem/src)
endif()

if(CONFIG_DRIVER_PIPE)
  add_subdirectory(pipe/src)
endif()

if(CONFIG_DRIVER_DYNLIB)
  add_subdirectory(dynlib/src)
endif()

if(CONFIG_DRIVER_TRANSPORT)
  add_subdirectory(transport/src)
endif()

if(CONFIG_DRIVER_AUDIO)
  add_subdirectory(audio/src)
endif()

if(CONFIG_DRIVER_CONTROL)
  add_subdirectory(control/src)
endif()

if(CONFIG_DRIVER_LOG)
  add_subdirectory(log/src)
endif()

if(CONFIG_DRIVER_DUMP)
  add_subdirectory(dump/src)
endif()

if(CONFIG_DRIVERS_CORE)
  add_subdirectory(src)
endif()
