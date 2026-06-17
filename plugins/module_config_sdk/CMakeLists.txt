cmake_minimum_required(VERSION 3.16)

project(audio_studio_module_config_sdk
  VERSION 1.0.0
  DESCRIPTION "Audio Studio module config plugin SDK"
  LANGUAGES CXX)

add_library(audio_studio_module_config_sdk INTERFACE)
add_library(AudioStudio::ModuleConfigSdk ALIAS audio_studio_module_config_sdk)

target_include_directories(audio_studio_module_config_sdk INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
target_compile_features(audio_studio_module_config_sdk INTERFACE cxx_std_17)
