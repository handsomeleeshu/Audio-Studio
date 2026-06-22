if(CONFIG_FRAMEWORK_COMMON)
  add_subdirectory(common)
endif()

if(CONFIG_FRAMEWORK_SESSION)
  add_subdirectory(session)
endif()

if(CONFIG_FRAMEWORK_CONTROL)
  add_subdirectory(control)
endif()

if(CONFIG_FRAMEWORK_AUDIO)
  add_subdirectory(audio)
endif()

if(CONFIG_FRAMEWORK_CONFIG)
  add_subdirectory(config)
endif()

if(CONFIG_FRAMEWORK_LOG)
  add_subdirectory(log)
endif()

if(CONFIG_FRAMEWORK_DUMP)
  add_subdirectory(dump)
endif()

if(CONFIG_FRAMEWORK_PLUGIN)
  add_subdirectory(plugin)
endif()

if(CONFIG_FRAMEWORK_TRANSPORT)
  add_subdirectory(transport)
endif()
