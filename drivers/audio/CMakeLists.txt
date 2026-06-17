set(AUDIO_STUDIO_DRIVER_AUDIO_SOURCES)
if(CONFIG_DRIVER_AUDIO_ALSA)
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
      "CONFIG_DRIVER_AUDIO_ALSA requires ALSA development files "
      "(alsa/asoundlib.h and libasound). Install libasound2-dev on Ubuntu.")
  endif()
  list(APPEND AUDIO_STUDIO_DRIVER_AUDIO_SOURCES src/alsa_audio_device.cpp)
endif()
if(CONFIG_DRIVER_AUDIO_PULSE)
  find_path(AUDIO_STUDIO_PULSE_INCLUDE_DIR pulse/simple.h)
  find_library(AUDIO_STUDIO_PULSE_SIMPLE_LIBRARY
    NAMES pulse-simple
    PATHS
      "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
      "/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
      /usr/lib/x86_64-linux-gnu
      /lib/x86_64-linux-gnu
      /usr/lib
      /lib
  )
  find_library(AUDIO_STUDIO_PULSE_LIBRARY
    NAMES pulse
    PATHS
      "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
      "/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
      /usr/lib/x86_64-linux-gnu
      /lib/x86_64-linux-gnu
      /usr/lib
      /lib
  )
  if(NOT AUDIO_STUDIO_PULSE_INCLUDE_DIR OR NOT AUDIO_STUDIO_PULSE_SIMPLE_LIBRARY OR NOT AUDIO_STUDIO_PULSE_LIBRARY)
    message(FATAL_ERROR
      "CONFIG_DRIVER_AUDIO_PULSE requires PulseAudio development files "
      "(pulse/simple.h, libpulse-simple, libpulse). Install libpulse-dev on Ubuntu.")
  endif()
  list(APPEND AUDIO_STUDIO_DRIVER_AUDIO_SOURCES src/pulse_audio_device.cpp)
endif()
if(CONFIG_DRIVER_AUDIO_WASAPI)
  list(APPEND AUDIO_STUDIO_DRIVER_AUDIO_SOURCES src/wasapi_audio_device.cpp)
endif()
if(CONFIG_DRIVER_AUDIO_MACOS)
  # macOS CoreAudio requires CoreAudio.framework and AudioToolbox.framework
  list(APPEND AUDIO_STUDIO_DRIVER_AUDIO_SOURCES src/macos_core_audio_device.cpp)
endif()
audio_studio_require_sources(audio_studio_driver_audio "${AUDIO_STUDIO_DRIVER_AUDIO_SOURCES}")

add_library(audio_studio_driver_audio OBJECT
  ${AUDIO_STUDIO_DRIVER_AUDIO_SOURCES}
)
target_include_directories(audio_studio_driver_audio PUBLIC include)
if(CONFIG_DRIVER_AUDIO_ALSA)
  target_include_directories(audio_studio_driver_audio PRIVATE "${AUDIO_STUDIO_ALSA_INCLUDE_DIR}")
  target_link_libraries(audio_studio_driver_audio PUBLIC "${AUDIO_STUDIO_ALSA_LIBRARY}")
endif()
if(CONFIG_DRIVER_AUDIO_PULSE)
  target_include_directories(audio_studio_driver_audio PRIVATE "${AUDIO_STUDIO_PULSE_INCLUDE_DIR}")
  target_link_libraries(audio_studio_driver_audio PUBLIC "${AUDIO_STUDIO_PULSE_SIMPLE_LIBRARY}" "${AUDIO_STUDIO_PULSE_LIBRARY}")
endif()
if(CONFIG_DRIVER_AUDIO_WASAPI)
  target_link_libraries(audio_studio_driver_audio PUBLIC ole32 uuid)
endif()
if(CONFIG_DRIVER_AUDIO_MACOS)
  # macOS CoreAudio uses AudioToolbox framework (AudioQueue API)
  if(APPLE)
    target_link_libraries(audio_studio_driver_audio PUBLIC "-framework CoreAudio" "-framework AudioToolbox")
  endif()
endif()
target_link_libraries(audio_studio_driver_audio PUBLIC audio_studio_framework_common)
target_compile_features(audio_studio_driver_audio PUBLIC cxx_std_17)
