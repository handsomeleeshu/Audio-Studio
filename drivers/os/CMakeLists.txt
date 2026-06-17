set(AUDIO_STUDIO_DRIVER_OS_SOURCES)
if(CONFIG_DRIVER_OS_LINUX_HOST)
  list(APPEND AUDIO_STUDIO_DRIVER_OS_SOURCES src/linux_host_os_driver.cpp)
endif()
if(CONFIG_DRIVER_OS_MACOS)
  list(APPEND AUDIO_STUDIO_DRIVER_OS_SOURCES src/macos_os_driver.cpp)
endif()
audio_studio_require_sources(audio_studio_driver_os "${AUDIO_STUDIO_DRIVER_OS_SOURCES}")

add_library(audio_studio_driver_os OBJECT
  ${AUDIO_STUDIO_DRIVER_OS_SOURCES}
)
target_include_directories(audio_studio_driver_os PUBLIC include)
target_link_libraries(audio_studio_driver_os PUBLIC audio_studio_framework_common)
target_compile_features(audio_studio_driver_os PUBLIC cxx_std_17)
