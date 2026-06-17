set(AUDIO_STUDIO_DRIVER_DUMP_SOURCES)
if(CONFIG_DRIVER_DUMP_LINUX_HOST)
  list(APPEND AUDIO_STUDIO_DRIVER_DUMP_SOURCES src/linux_host_dump_device.cpp)
endif()
if(CONFIG_DRIVER_DUMP_MACOS)
  list(APPEND AUDIO_STUDIO_DRIVER_DUMP_SOURCES src/macos_dump_device.cpp)
endif()
audio_studio_require_sources(audio_studio_driver_dump "${AUDIO_STUDIO_DRIVER_DUMP_SOURCES}")

add_library(audio_studio_driver_dump OBJECT
  ${AUDIO_STUDIO_DRIVER_DUMP_SOURCES}
)
target_include_directories(audio_studio_driver_dump PUBLIC include)
target_link_libraries(audio_studio_driver_dump PUBLIC audio_studio_framework_common)
target_compile_features(audio_studio_driver_dump PUBLIC cxx_std_17)
