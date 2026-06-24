add_library(audio_studio_framework_system_info
  src/system_info_service.cpp
)
target_include_directories(audio_studio_framework_system_info PUBLIC
  include
  "${PROJECT_SOURCE_DIR}/server/framework/log/include")
target_link_libraries(audio_studio_framework_system_info PUBLIC
  audio_studio_framework_common
  audio_studio_framework_log)
target_compile_features(audio_studio_framework_system_info PUBLIC cxx_std_17)
