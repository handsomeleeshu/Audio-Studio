cmake_minimum_required(VERSION 3.16)

if(CONFIG_FRAMEWORK_COMMON)
  add_library(audio_studio_framework_common
    framework/common/src/service_registry.cpp
    framework/common/src/status.cpp
  )
  target_include_directories(audio_studio_framework_common PUBLIC framework/common/include)
  target_compile_features(audio_studio_framework_common PUBLIC cxx_std_17)
endif()

if(CONFIG_DRIVER_DUMMY)
  add_library(audio_studio_dummy_driver
    drivers/dummy/src/dummy_driver.cpp
  )
  target_include_directories(audio_studio_dummy_driver PUBLIC drivers/dummy/include)
  target_link_libraries(audio_studio_dummy_driver PUBLIC audio_studio_framework_common)
  target_compile_features(audio_studio_dummy_driver PUBLIC cxx_std_17)
endif()

add_executable(as_server as_server/main.cpp)
target_link_libraries(as_server PRIVATE audio_studio_framework_common audio_studio_dummy_driver)
target_compile_features(as_server PRIVATE cxx_std_17)

if(CONFIG_BUILD_TESTS)
  add_executable(audio_studio_server_tests
    tests/server_tests.cpp
  )
  target_link_libraries(audio_studio_server_tests PRIVATE audio_studio_framework_common audio_studio_dummy_driver)
  target_compile_features(audio_studio_server_tests PRIVATE cxx_std_17)
  add_test(NAME server_framework_tests COMMAND audio_studio_server_tests)
endif()
