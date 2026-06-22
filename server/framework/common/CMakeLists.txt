add_library(audio_studio_framework_common
  src/service_registry.cpp
  src/status.cpp
)
target_include_directories(audio_studio_framework_common PUBLIC include)
target_compile_features(audio_studio_framework_common PUBLIC cxx_std_17)
