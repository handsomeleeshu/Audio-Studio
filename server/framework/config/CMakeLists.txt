add_library(audio_studio_framework_config
  src/config_service.cpp
)
target_include_directories(audio_studio_framework_config PUBLIC
  include
  "${PROJECT_SOURCE_DIR}/drivers/filesystem/include"
  "${PROJECT_SOURCE_DIR}/drivers/os/include"
  "${PROJECT_SOURCE_DIR}/drivers/dynlib/include"
)
target_link_libraries(audio_studio_framework_config PUBLIC
  audio_studio_framework_common
  audio_studio_json
  AudioStudio::ModuleConfigSdk
)
target_compile_features(audio_studio_framework_config PUBLIC cxx_std_17)
