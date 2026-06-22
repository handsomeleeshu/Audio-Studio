add_library(audio_studio_platform_simulator OBJECT
  src/rv32qemu_audio_device.cpp
  src/rv32qemu_log_device.cpp
  src/simulator_pipe_datalink_device.cpp
  src/simulator_platform.cpp
)
target_include_directories(audio_studio_platform_simulator PUBLIC
  include
  "${PROJECT_SOURCE_DIR}/audio_controller/include"
  "${PROJECT_SOURCE_DIR}/audio_controller/src")
target_link_libraries(audio_studio_platform_simulator PUBLIC
  audio_studio_platform_core
  audio_studio_driver_audio
  audio_studio_driver_log
  audio_studio_driver_datalink
  audio_studio_framework_transport)
target_compile_features(audio_studio_platform_simulator PUBLIC cxx_std_17)
