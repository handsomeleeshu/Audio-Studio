set(AUDIO_STUDIO_DRIVER_PIPE_SOURCES)
if(CONFIG_DRIVER_PIPE_LINUX_HOST)
  list(APPEND AUDIO_STUDIO_DRIVER_PIPE_SOURCES src/linux_host_pipe_driver.cpp)
endif()
if(CONFIG_DRIVER_PIPE_MACOS)
  list(APPEND AUDIO_STUDIO_DRIVER_PIPE_SOURCES src/macos_pipe_driver.cpp)
endif()
audio_studio_require_sources(audio_studio_driver_pipe "${AUDIO_STUDIO_DRIVER_PIPE_SOURCES}")

add_library(audio_studio_driver_pipe OBJECT
  ${AUDIO_STUDIO_DRIVER_PIPE_SOURCES}
)
target_include_directories(audio_studio_driver_pipe PUBLIC include)
target_link_libraries(audio_studio_driver_pipe PUBLIC audio_studio_framework_common)
target_compile_features(audio_studio_driver_pipe PUBLIC cxx_std_17)
