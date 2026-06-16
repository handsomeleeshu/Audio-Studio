cmake_minimum_required(VERSION 3.16)

add_library(audio_studio_cli_common
  common/src/cli_common.cpp
)
target_include_directories(audio_studio_cli_common PUBLIC common/include)
target_link_libraries(audio_studio_cli_common PUBLIC audio_studio_dummy_driver)
target_compile_features(audio_studio_cli_common PUBLIC cxx_std_17)

foreach(tool as_control as_play as_record as_log as_dump)
  add_executable(${tool} tools/${tool}.cpp)
  target_link_libraries(${tool} PRIVATE audio_studio_cli_common)
  target_compile_features(${tool} PRIVATE cxx_std_17)
endforeach()

if(CONFIG_BUILD_TESTS)
  add_executable(audio_studio_cli_tests tests/cli_tests.cpp)
  target_link_libraries(audio_studio_cli_tests PRIVATE audio_studio_cli_common audio_studio_dummy_driver)
  target_compile_features(audio_studio_cli_tests PRIVATE cxx_std_17)
  add_test(NAME cli_tests COMMAND audio_studio_cli_tests)
endif()
