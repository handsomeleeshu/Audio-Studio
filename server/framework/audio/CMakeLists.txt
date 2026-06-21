add_library(audio_studio_framework_audio
  src/audio_service.cpp
)
target_include_directories(audio_studio_framework_audio PUBLIC
  include
  "${PROJECT_SOURCE_DIR}/drivers/audio/include"
)
target_link_libraries(audio_studio_framework_audio PUBLIC audio_studio_framework_common)
target_compile_features(audio_studio_framework_audio PUBLIC cxx_std_17)
