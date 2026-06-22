add_library(audio_studio_framework_transport
  src/datalink_frame.cpp
  src/datalink_manager.cpp
  src/frame_codec.cpp
  src/transport_manager.cpp
)
target_include_directories(audio_studio_framework_transport PUBLIC
  include
  "${PROJECT_SOURCE_DIR}/drivers/datalink/include")
target_link_libraries(audio_studio_framework_transport PUBLIC audio_studio_framework_common)
target_compile_features(audio_studio_framework_transport PUBLIC cxx_std_17)
