add_executable(as_server
  main/as_server_main.cpp
  main/server_app.cpp
)

target_include_directories(as_server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/main
)

target_compile_features(as_server PRIVATE cxx_std_17)
