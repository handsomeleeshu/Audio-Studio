enable_language(C)

set(AUDIO_STUDIO_SOF_ROOT "${PROJECT_SOURCE_DIR}/../sof" CACHE PATH
  "Path to the VASS SOF source tree for SOF log decoding")

add_library(audio_studio_framework_log
  src/log_service.cpp
  src/sof_logger_decoder_c.c
  "${AUDIO_STUDIO_SOF_ROOT}/tools/logger/convert.c"
  "${AUDIO_STUDIO_SOF_ROOT}/tools/logger/filter.c"
  "${AUDIO_STUDIO_SOF_ROOT}/tools/logger/misc.c"
)
target_include_directories(audio_studio_framework_log PUBLIC
  include
  "${PROJECT_SOURCE_DIR}/drivers/log/include")
target_include_directories(audio_studio_framework_log PRIVATE
  shim
  "${AUDIO_STUDIO_SOF_ROOT}"
  "${AUDIO_STUDIO_SOF_ROOT}/tools/logger"
  "${AUDIO_STUDIO_SOF_ROOT}/posix/include"
  "${AUDIO_STUDIO_SOF_ROOT}/src/include")
target_link_libraries(audio_studio_framework_log PUBLIC audio_studio_framework_common)
target_compile_features(audio_studio_framework_log PUBLIC cxx_std_17)
