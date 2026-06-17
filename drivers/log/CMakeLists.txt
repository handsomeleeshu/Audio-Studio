set(AUDIO_STUDIO_DRIVER_LOG_SOURCES)
if(CONFIG_DRIVER_LOG_LINUX_HOST)
  list(APPEND AUDIO_STUDIO_DRIVER_LOG_SOURCES src/linux_host_log_device.cpp)
endif()
if(CONFIG_DRIVER_LOG_MACOS)
  list(APPEND AUDIO_STUDIO_DRIVER_LOG_SOURCES src/macos_log_device.cpp)
endif()
audio_studio_require_sources(audio_studio_driver_log "${AUDIO_STUDIO_DRIVER_LOG_SOURCES}")

add_library(audio_studio_driver_log OBJECT
  ${AUDIO_STUDIO_DRIVER_LOG_SOURCES}
)
target_include_directories(audio_studio_driver_log PUBLIC include)
target_link_libraries(audio_studio_driver_log PUBLIC audio_studio_framework_common)
target_compile_features(audio_studio_driver_log PUBLIC cxx_std_17)
