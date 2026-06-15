add_executable(audio_controller
  common/ac_main.c
)

target_include_directories(audio_controller PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(audio_controller PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
)
