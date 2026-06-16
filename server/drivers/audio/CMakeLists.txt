set(AUDIO_STUDIO_DRIVER_AUDIO_SOURCES)
if(CONFIG_DRIVER_AUDIO_LINUX_HOST)
  find_path(AUDIO_STUDIO_ALSA_INCLUDE_DIR alsa/asoundlib.h)
  find_library(AUDIO_STUDIO_ALSA_LIBRARY
    NAMES asound
    PATHS
      "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
      "/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
      /usr/lib/x86_64-linux-gnu
      /lib/x86_64-linux-gnu
      /usr/lib
      /lib
  )
  if(NOT AUDIO_STUDIO_ALSA_INCLUDE_DIR OR NOT AUDIO_STUDIO_ALSA_LIBRARY)
    message(FATAL_ERROR
      "CONFIG_DRIVER_AUDIO_LINUX_HOST requires ALSA development files "
      "(alsa/asoundlib.h and libasound). Install libasound2-dev on Ubuntu.")
  endif()
  list(APPEND AUDIO_STUDIO_DRIVER_AUDIO_SOURCES src/linux_host_audio_device.cpp)
endif()
audio_studio_require_sources(audio_studio_driver_audio "${AUDIO_STUDIO_DRIVER_AUDIO_SOURCES}")

add_library(audio_studio_driver_audio OBJECT
  ${AUDIO_STUDIO_DRIVER_AUDIO_SOURCES}
)
target_include_directories(audio_studio_driver_audio PUBLIC include)
if(CONFIG_DRIVER_AUDIO_LINUX_HOST)
  target_include_directories(audio_studio_driver_audio PRIVATE "${AUDIO_STUDIO_ALSA_INCLUDE_DIR}")
  target_link_libraries(audio_studio_driver_audio PUBLIC "${AUDIO_STUDIO_ALSA_LIBRARY}")
endif()
target_link_libraries(audio_studio_driver_audio PUBLIC audio_studio_framework_common)
target_compile_features(audio_studio_driver_audio PUBLIC cxx_std_17)
