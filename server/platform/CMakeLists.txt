if(CONFIG_PLATFORM_CORE)
  add_subdirectory(core)
endif()

if(CONFIG_PLATFORM_A2)
  add_subdirectory(a2)
endif()

if(CONFIG_PLATFORM_SIMULATOR)
  add_subdirectory(simulator)
endif()
