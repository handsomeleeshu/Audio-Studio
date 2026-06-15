function(add_audio_studio_cli_tool tool_name)
  add_executable(${tool_name}
    common/cli_stub_main.cpp
  )
  target_compile_features(${tool_name} PRIVATE cxx_std_17)
  target_compile_definitions(${tool_name} PRIVATE
    AUDIO_STUDIO_CLI_TOOL_NAME="${tool_name}"
  )
endfunction()

add_audio_studio_cli_tool(as_config)
add_audio_studio_cli_tool(as_control)
add_audio_studio_cli_tool(as_play)
add_audio_studio_cli_tool(as_record)
add_audio_studio_cli_tool(as_log)
add_audio_studio_cli_tool(as_dump)
