cmake_minimum_required(VERSION 3.16)

project(third_party_module_config
  VERSION 1.0.0
  DESCRIPTION "Standalone Audio Studio third-party module config plugin example"
  LANGUAGES CXX)

add_subdirectory("../.." "${CMAKE_BINARY_DIR}/module_config_sdk")

add_library(as_third_party_module_config SHARED
  src/third_party_module_config.cpp
)
target_link_libraries(as_third_party_module_config PRIVATE AudioStudio::ModuleConfigSdk)
target_compile_features(as_third_party_module_config PRIVATE cxx_std_17)
