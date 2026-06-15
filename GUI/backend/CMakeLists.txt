cmake_minimum_required(VERSION 3.16)
project(audio_studio_backend LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(audio_studio_core
  src/mock_runtime.cpp
  src/http_server.cpp
)
target_include_directories(audio_studio_core PUBLIC include)

add_executable(audio_studio_server src/main.cpp)
target_link_libraries(audio_studio_server PRIVATE audio_studio_core)

add_executable(audio_studio_backend_tests tests/backend_tests.cpp)
target_link_libraries(audio_studio_backend_tests PRIVATE audio_studio_core)

enable_testing()
add_test(NAME backend_tests COMMAND audio_studio_backend_tests)
