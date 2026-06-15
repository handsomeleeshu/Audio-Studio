add_library(audio_studio_drivers_core
  driver_manager.cpp
)
target_include_directories(audio_studio_drivers_core PUBLIC
  ../include
  ../audio/include
  ../control/include
  ../dump/include
  ../dynlib/include
  ../filesystem/include
  ../log/include
  ../os/include
  ../pipe/include
  ../socket/include
  ../transport/include
)
target_link_libraries(audio_studio_drivers_core PUBLIC
  audio_studio_config
  audio_studio_framework_common
)
foreach(driver_target
    audio_studio_driver_audio
    audio_studio_driver_control
    audio_studio_driver_dump
    audio_studio_driver_dynlib
    audio_studio_driver_filesystem
    audio_studio_driver_log
    audio_studio_driver_os
    audio_studio_driver_pipe
    audio_studio_driver_socket
    audio_studio_driver_transport)
  if(TARGET ${driver_target})
    target_link_libraries(audio_studio_drivers_core PUBLIC ${driver_target})
  endif()
endforeach()
target_compile_features(audio_studio_drivers_core PUBLIC cxx_std_17)
