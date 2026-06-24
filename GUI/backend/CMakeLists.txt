cmake_minimum_required(VERSION 3.16)
project(audio_studio_backend LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(audio_studio_core
  src/mock_runtime.cpp
  src/http_server.cpp
  src/project_orchestration.cpp
  src/system_info_controllers.cpp
)
target_include_directories(audio_studio_core PUBLIC include)
target_link_libraries(audio_studio_core PUBLIC audio_studio_config)
if(CONFIG_RPC_CORE)
  target_link_libraries(audio_studio_core PUBLIC audio_studio_rpc_core)
  target_compile_definitions(audio_studio_core PRIVATE AUDIO_STUDIO_GUI_BACKEND_RPC=1)
endif()
if(CONFIG_RPC_CLIENT)
  target_link_libraries(audio_studio_core PUBLIC audio_studio_rpc_client audio_studio_drivers_core)
endif()

add_executable(audio_studio_server src/main.cpp)
target_link_libraries(audio_studio_server PRIVATE audio_studio_core)

add_executable(audio_studio_backend_tests tests/backend_tests.cpp)
target_link_libraries(audio_studio_backend_tests PRIVATE audio_studio_core)
target_compile_definitions(audio_studio_backend_tests PRIVATE
  AUDIO_STUDIO_TEST_ROOT="${CMAKE_SOURCE_DIR}"
)

enable_testing()
add_test(NAME backend_tests COMMAND audio_studio_backend_tests)
