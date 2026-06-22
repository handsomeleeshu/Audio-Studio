#!/usr/bin/env python3
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import wave


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_ALL = ROOT / 'scripts' / 'build_all.sh'
LINUX_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'as_server_minimal' / 'Debug'
DRIVER_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'driver_interface_tests' / 'Debug'
GUI_BACKEND_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'gui_backend' / 'Release'
AS_CONFIG_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'as_config' / 'Debug'
AUDIO_CONTROLLER_BUILD_DIR = ROOT / 'out' / 'audio_controller'
AUDIO_CONTROLLER_PROFILE_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'audio_controller' / 'Debug'
MODULE_CONFIG_EXAMPLE_BUILD_DIR = ROOT / 'out' / 'module-config-sdk-example'
PREBUILT_ALSATPLG = ROOT / 'third_party' / 'alsatplg' / 'bin' / 'alsatplg'
RPC_SOCKET_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'rpc_socket' / 'Debug'
RPC_SOCKET_SIMULATOR_BUILD_DIR = ROOT / 'out' / 'linux' / 'simulator' / 'rpc_socket' / 'Debug'
RPC_PIPE_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'rpc_pipe' / 'Debug'
WINDOWS_BUILD_DIR = ROOT / 'out' / 'windows' / 'a2' / 'as_server_minimal' / 'Debug'
WINDOWS_RPC_SOCKET_BUILD_DIR = ROOT / 'out' / 'windows' / 'a2' / 'rpc_socket' / 'Debug'
A2_PLATFORM_DIR = ROOT / 'configs' / 'platform' / 'a2'
SIMULATOR_PLATFORM_DIR = ROOT / 'configs' / 'platform' / 'simulator'
A2_PROJECT_JSON = A2_PLATFORM_DIR / 'A2.json'
SIMULATOR_PROJECT_JSON = SIMULATOR_PLATFORM_DIR / 'simulator.json'
BUILTIN_ALGORITHM_JSON = ROOT / 'configs' / 'built-in-algorithm.json'


def run(command, **kwargs):
    return subprocess.run(command, cwd=str(ROOT), check=True, **kwargs)


def check_output(command, **kwargs):
    kwargs.setdefault('cwd', str(ROOT))
    return subprocess.check_output(command, text=True, **kwargs)


def wine_check_output(command):
    return check_output(['wine64', *map(str, command)])


def retry_check_output(command, attempts=20, delay=0.1):
    last_error = None
    for _ in range(attempts):
        try:
            return check_output(command)
        except subprocess.CalledProcessError as exc:
            last_error = exc
            time.sleep(delay)
    raise last_error


def retry_wine_check_output(command, attempts=20, delay=0.1):
    return retry_check_output(['wine64', *map(str, command)], attempts, delay)


def wine_path(path):
    return check_output(['winepath', '-w', str(path)]).strip()


def find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(('127.0.0.1', 0))
        return str(sock.getsockname()[1])


def write_test_wav(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = bytes(9600)
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(48000)
        wav.writeframes(payload)


def write_module_config_plugin_project(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        '''{
  "module_types": [
    {
      "type_id": "test.third_party",
      "category": "test/plugin",
      "module_class": "MODULE_ADAPTER",
      "module_uuid": "2e3a1183-7da0-4a3d-b463-1b3b4e7a51ed",
      "parameters": [
        {
          "param_id": "knob",
          "param_name": "Knob",
          "value_type": "uint8",
          "default": 7,
          "apply": {
            "settable_states": ["PIPE_LOADED", "RUNNING"],
            "mode": "next_frame"
          }
        }
      ]
    }
  ],
  "module_instances": [
    {"inst_id": "TP0", "name": "Third Party", "module_type": "test.third_party"}
  ],
  "pipelines": [
    {
      "pipe_id": "PLUGIN_PIPE",
      "name": "Plugin Pipe",
      "domain": "playback",
      "frame": {"rate": 48000},
      "ports": [],
      "nodes": [
        {"node_id": "IN", "kind": "port", "port_ref": "P_IN"},
        {"node_id": "TP", "kind": "module", "inst_ref": "TP0"},
        {"node_id": "OUT", "kind": "port", "port_ref": "P_OUT"}
      ],
      "edges": [
        {"from": "IN:out", "to": "TP:in"},
        {"from": "TP:out", "to": "OUT:in"}
      ]
    }
  ],
  "presets": [
    {
      "preset_id": "plugin.default",
      "load_mode": "bulk",
      "node_values": [
        {
          "pipeline_id": "PLUGIN_PIPE",
          "node_id": "TP",
          "values": {"knob": 9}
        }
      ]
    }
  ]
}
''',
        encoding='utf-8',
    )


def assert_wav(path, expected_frames):
    with wave.open(str(path), 'rb') as wav:
        assert wav.getnchannels() == 2
        assert wav.getsampwidth() == 2
        assert wav.getframerate() == 48000
        assert wav.getnframes() == expected_frames


def wait_for_paths(paths, process, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if all(path.exists() for path in paths):
            return
        if process.poll() is not None:
            stderr = process.stderr.read() if process.stderr else ''
            raise AssertionError(f'process exited before paths were ready: {stderr}')
        time.sleep(0.05)
    raise TimeoutError(f'timed out waiting for paths: {paths}')


def read_text(path):
    return path.read_text(encoding='utf-8')


def cmake_target(target):
    run(['cmake', '--build', str(LINUX_BUILD_DIR), '--target', target])


def require_contains(text, expected):
    assert expected in text, f'missing {expected!r}'


def assert_datalink_driver_naming():
    assert (ROOT / 'drivers' / 'datalink' / 'include' / 'datalink_device.hpp').exists()
    assert not (ROOT / 'drivers' / 'transport' / 'include' / 'transport_driver.hpp').exists()
    assert not (ROOT / 'audio_controller' / 'platform').exists()

    forbidden = (
        'ITransportDriver',
        'ITransportDriverFactory',
        'TransportDriverRegistry',
        'linux_host_transport_driver',
        'macos_transport_driver',
        'CONFIG_DRIVER_TRANSPORT',
    )
    for root in (
        ROOT / 'drivers',
        ROOT / 'server',
        ROOT / 'configs',
        ROOT / 'audio_controller',
    ):
        for source in root.rglob('*'):
            if source.is_dir() or source.suffix not in ('.cpp', '.hpp', '.h', '.c', '.txt', '.cmake', ''):
                continue
            text = source.read_text(errors='ignore')
            for expected_absent in forbidden:
                assert expected_absent not in text, (
                    f'{expected_absent} still appears in {source.relative_to(ROOT)}')


def assert_as_log_embeds_sof_decoder():
    cli_common = read_text(ROOT / 'cli' / 'common' / 'src' / 'cli_common.cpp')
    rpc_api = read_text(ROOT / 'rpc' / 'api' / 'audio_studio_rpc_api.cpp')
    log_service = read_text(ROOT / 'server' / 'framework' / 'log' / 'src' / 'log_service.cpp')
    server_cmake = read_text(ROOT / 'server' / 'CMakeLists.txt')
    log_cmake = read_text(ROOT / 'server' / 'framework' / 'log' / 'CMakeLists.txt')

    assert '--sof-logger' not in cli_common
    assert 'sof_logger' not in cli_common
    assert 'sof_logger' not in rpc_api
    assert 'std::system' not in log_service
    assert 'tools/logger/convert.c' not in server_cmake
    require_contains(log_cmake, 'tools/logger/convert.c')


def assert_sof_test_ac_run_is_platform_neutral():
    ac_run = read_text(ROOT.parent / 'Misc' / 'sof_test' / 'ac-run-cmds.c')
    forbidden = (
        'rv32qemu',
        'SOF_TEST_PLATFORM',
        'ac_rv32qemu',
        'sof_ipc_set_trace_bytes_sink',
        'audio_controller_append_log_data',
    )
    for expected_absent in forbidden:
        assert expected_absent not in ac_run


def assert_as_log_cli_is_transport_neutral():
    cli_common = read_text(ROOT / 'cli' / 'common' / 'src' / 'cli_common.cpp')
    as_server = read_text(ROOT / 'server' / 'as_server' / 'main.cpp')
    log_service_header = read_text(ROOT / 'server' / 'framework' / 'log' / 'include' / 'log_service.hpp')
    rv32_helper = read_text(ROOT.parent / 'application' / 'rv32qemu' / 'sof-build-test.py')

    assert 'if (tool == "as_log") options.driver_factory = "linux-host";' not in cli_common
    assert 'if (tool != "as_log")' in cli_common
    for forbidden_param in ('params["driver_factory"]', 'params["datalink_endpoint"]', 'params["trace_ldc"]'):
        assert forbidden_param not in cli_common
    require_contains(as_server, '--log-driver-factory')
    require_contains(as_server, '--log-datalink-endpoint')
    require_contains(as_server, '--log-trace-ldc')
    require_contains(log_service_header, 'setDefaultSessionConfig')
    require_contains(rv32_helper, '"--log-driver-factory", "rv32qemu"')
    require_contains(rv32_helper, '"--log-datalink-endpoint", datalink_endpoint')
    require_contains(rv32_helper, '"--log-datalink-mtu", "512"')
    require_contains(rv32_helper, '"--log-trace-ldc", trace_ldc')

    as_log_section = rv32_helper.split('as_log_cmd = [', 1)[1].split(']', 1)[0]
    for forbidden in ('--driver-factory', '--datalink-endpoint', '--trace-ldc'):
        assert forbidden not in as_log_section


def assert_framework_build_config_is_modular():
    server_cmake = read_text(ROOT / 'server' / 'CMakeLists.txt')
    framework_kconfig = read_text(ROOT / 'server' / 'framework' / 'Kconfig')
    platform_kconfig = read_text(ROOT / 'server' / 'platform' / 'Kconfig')

    for module in ['common', 'session', 'control', 'audio', 'config', 'log', 'dump', 'plugin', 'transport']:
        assert (ROOT / 'server' / 'framework' / module / 'CMakeLists.txt').exists()
        assert (ROOT / 'server' / 'framework' / module / 'Kconfig').exists()
        assert f'source "server/framework/{module}/Kconfig"' in framework_kconfig
        assert f'framework/{module}/src/' not in server_cmake

    for module in ['core', 'a2', 'simulator']:
        assert (ROOT / 'server' / 'platform' / module / 'CMakeLists.txt').exists()
        assert (ROOT / 'server' / 'platform' / module / 'Kconfig').exists()
        assert f'source "server/platform/{module}/Kconfig"' in platform_kconfig
        assert f'platform/{module}/src/' not in server_cmake

    assert 'add_subdirectory(framework)' in server_cmake
    assert 'add_subdirectory(platform)' in server_cmake


def assert_audio_controller_transport_channels_are_layered():
    transport_c = read_text(ROOT / 'audio_controller' / 'src' / 'ac_transport.c')
    transport_h = read_text(ROOT / 'audio_controller' / 'src' / 'ac_transport.h')
    log_c = read_text(ROOT / 'audio_controller' / 'src' / 'ac_log.c')
    channel_h = read_text(ROOT / 'audio_controller' / 'src' / 'ac_transport_channel.h')
    rv32_log_device = read_text(ROOT / 'server' / 'platform' / 'simulator' / 'src' / 'rv32qemu_log_device.cpp')

    assert 'ac_transport_handle_log' not in transport_c
    assert 'ac_log_transport_handler' in log_c
    assert 'ac_transport_register_channel' in transport_h
    assert 'ac_transport_open_channel' in transport_h
    assert 'AC_TRANSPORT_CHANNEL_LOG' in channel_h
    assert 'AC_TRANSPORT_CHANNEL_DUMP' in channel_h
    assert '#include "ac_transport_channel.h"' in rv32_log_device
    assert 'kLogChannelId = 1' not in rv32_log_device


def assert_simulator_audio_transport_channels_are_stream_scoped():
    transport_c = read_text(ROOT / 'audio_controller' / 'src' / 'ac_transport.c')
    transport_h = read_text(ROOT / 'audio_controller' / 'src' / 'ac_transport.h')
    channel_h = read_text(ROOT / 'audio_controller' / 'src' / 'ac_transport_channel.h')
    ac_audio_c = read_text(ROOT / 'audio_controller' / 'src' / 'ac_audio.c')
    simulator_cmake = read_text(ROOT / 'server' / 'platform' / 'simulator' / 'CMakeLists.txt')
    transport_manager_h = read_text(ROOT / 'server' / 'framework' / 'transport' / 'include' / 'transport_manager.hpp')
    transport_manager_cpp = read_text(ROOT / 'server' / 'framework' / 'transport' / 'src' / 'transport_manager.cpp')
    rv32_log_device = read_text(ROOT / 'server' / 'platform' / 'simulator' / 'src' / 'rv32qemu_log_device.cpp')
    rv32_audio_device = read_text(ROOT / 'server' / 'platform' / 'simulator' / 'src' / 'rv32qemu_audio_device.cpp')

    require_contains(channel_h, 'AC_TRANSPORT_CHANNEL_AUDIO_CONTROL 3u')
    require_contains(channel_h, 'AC_TRANSPORT_AUDIO_MAX_STREAMS 16u')
    require_contains(channel_h, 'AC_TRANSPORT_AUDIO_DATA_CHANNEL_FIRST 4u')
    require_contains(channel_h, 'AC_TRANSPORT_AUDIO_DATA_CHANNEL_LAST 19u')
    require_contains(transport_h, 'AC_TRANSPORT_MAX_CHANNELS 20u')
    assert 'sof_stream_' not in transport_c
    require_contains(ac_audio_c, 'sof_stream_open')
    require_contains(ac_audio_c, 'sof_stream_get_free_size')
    require_contains(ac_audio_c, 'sof_stream_get_avail_size')
    require_contains(ac_audio_c, 'AC_TRANSPORT_AUDIO_DRAIN')
    ac_audio_tests = read_text(ROOT / 'audio_controller' / 'tests' / 'ac_transport_tests.c')
    require_contains(ac_audio_tests, 'test_audio_write_uses_blocking_write')
    require_contains(ac_audio_tests, 'test_audio_read_uses_blocking_read')
    control_handler = ac_audio_c.split('ac_audio_control_transport_handler', 1)[1].split('ac_audio_data_transport_handler', 1)[0]
    data_handler = ac_audio_c.split('ac_audio_data_transport_handler', 1)[1]
    assert 'AC_TRANSPORT_AUDIO_WRITE' not in control_handler
    assert 'AC_TRANSPORT_AUDIO_READ' not in control_handler
    assert 'AC_TRANSPORT_AUDIO_DRAIN' not in control_handler
    require_contains(data_handler, 'AC_TRANSPORT_AUDIO_WRITE')
    require_contains(data_handler, 'AC_TRANSPORT_AUDIO_READ')
    require_contains(data_handler, 'AC_TRANSPORT_AUDIO_DRAIN')
    require_contains(simulator_cmake, 'rv32qemu_audio_device.cpp')
    require_contains(transport_manager_h, 'static TransportManager& instance()')
    require_contains(transport_manager_h, 'TransportManager(const TransportManager&) = delete')
    require_contains(transport_manager_cpp, 'transport channel already exists')
    require_contains(transport_manager_h, 'bool isDataLinkConfigured() const')
    assert 'acquireSharedChannel' not in transport_manager_h
    assert 'TransportChannelMode' not in transport_manager_h
    assert 'configureDataLinkDevice("simulator-pipe"' not in rv32_log_device
    assert 'configureDataLinkDevice("simulator-pipe"' not in rv32_audio_device
    assert 'optionString' not in rv32_audio_device
    assert 'rx_path' not in rv32_audio_device
    assert 'tx_path' not in rv32_audio_device
    assert 'make_unique<framework::transport::TransportManager>' not in rv32_log_device
    assert 'make_unique<framework::transport::TransportManager>' not in rv32_audio_device
    require_contains(rv32_log_device, 'TransportManager::instance()')
    require_contains(rv32_audio_device, 'TransportManager::instance()')
    require_contains(rv32_audio_device, 'isDataLinkConfigured()')
    require_contains(rv32_audio_device, 'audioControlChannelRefs')
    require_contains(rv32_audio_device, 'releaseAudioControlChannel')


def assert_audio_cli_lets_server_select_default_driver():
    cli_common = read_text(ROOT / 'cli' / 'common' / 'src' / 'cli_common.cpp')
    rpc_api = read_text(ROOT / 'rpc' / 'api' / 'audio_studio_rpc_api.cpp')
    audio_service_h = read_text(ROOT / 'server' / 'framework' / 'audio' / 'include' / 'audio_service.hpp')
    as_server = read_text(ROOT / 'server' / 'as_server' / 'main.cpp')
    rv32_helper = read_text(ROOT.parent / 'application' / 'rv32qemu' / 'sof-build-test.py')
    rv32_audio_case = read_text(ROOT.parent / 'Misc' / 'sof_test' / 'simple_test' / 'rv32qemu-as-audio-controller-test-lists.txt')

    audio_session_parser = cli_common.split('rpc::AudioSessionConfig audioSessionConfigFromArgs', 1)[1].split('std::string rpcTransportName', 1)[0]
    assert 'defaultAudioDriverFactory()' not in audio_session_parser
    require_contains(audio_session_parser, 'args.valueAfter("--driver-factory", "")')
    require_contains(audio_session_parser, 'args.valueAfter("--device", "")')
    rpc_audio_parser = rpc_api.split('framework::audio::AudioStream audioStreamFromParams', 1)[1].split('JsonValue createAudioSession', 1)[0]
    assert 'defaultAudioDriverFactory()' not in rpc_audio_parser
    require_contains(rpc_audio_parser, 'optionalStringParam(object, "driver_factory", "")')
    require_contains(rpc_audio_parser, 'optionalStringParam(object, "device", "")')
    require_contains(audio_service_h, 'std::map<std::string, std::string> options')
    require_contains(as_server, '--audio-driver-factory')
    require_contains(as_server, '--audio-datalink-endpoint')
    require_contains(as_server, 'configureTransportDataLinkFromOptions')
    require_contains(as_server, 'TransportManager::instance().configureDataLinkDevice')
    default_audio_config = as_server.split('AudioServiceConfig defaultAudioConfigFromOptions', 1)[1].split('bool hasLogDataLinkOptions', 1)[0]
    assert 'config.options["endpoint"]' not in default_audio_config
    default_log_config = as_server.split('LogSessionConfig defaultLogConfigFromOptions', 1)[1].split('AudioServiceConfig defaultAudioConfigFromOptions', 1)[0]
    assert 'config.options["endpoint"]' not in default_log_config
    require_contains(rv32_helper, '"--audio-driver-factory", "rv32qemu"')
    require_contains(rv32_helper, '"--audio-datalink-endpoint", datalink_endpoint')
    require_contains(rv32_audio_case, 'ac_run --endpoint as_datalink --mtu 512')


def assert_sof_logger_follow_ignores_decoder_footer():
    log_service_h = read_text(ROOT / 'server' / 'framework' / 'log' / 'include' / 'log_service.hpp')
    log_service = read_text(ROOT / 'server' / 'framework' / 'log' / 'src' / 'log_service.cpp')
    decoder_c = read_text(ROOT / 'server' / 'framework' / 'log' / 'src' / 'sof_logger_decoder_c.c')

    require_contains(log_service_h, 'decoded_entries_read')
    assert 'decoded_lines_read' not in log_service_h
    require_contains(log_service, 'isSofLoggerDiagnosticLine')
    require_contains(log_service, 'Skipped ')
    require_contains(log_service, 'Potential mailbox wrap')
    require_contains(log_service, 'Found valid LDC address')
    require_contains(log_service, 'if (!isSofLoggerEntryLine(line)) continue;')
    require_contains(log_service, 'entry_index++ < session.decoded_entries_read')
    assert 'config.trace = 1' not in decoder_c


def assert_sof_test_ac_run_uses_getopt_long():
    ac_run = read_text(ROOT.parent / 'Misc' / 'sof_test' / 'ac-run-cmds.c')
    assert '#include <getopt.h>' in ac_run
    assert 'getopt_long' in ac_run
    assert 'ac_run_option_value' not in ac_run
    assert 'ac_run_has_option' not in ac_run


def assert_rv32_log_datalink_files_are_session_scoped():
    simulator_pipe = read_text(ROOT / 'server' / 'platform' / 'simulator' / 'src' / 'simulator_pipe_datalink_device.cpp')
    rv32_ac_platform = read_text(ROOT.parent / 'Misc' / 'sof_test' / 'platform' / 'rv32qemu' / 'ac_platform.c')

    assert 'if (::lstat(path.c_str(), &st) != 0) {' not in simulator_pipe
    assert 'O_CREAT | O_TRUNC | O_RDWR' not in simulator_pipe
    require_contains(simulator_pipe, 'O_CREAT | O_RDWR')
    require_contains(rv32_ac_platform, 'open(pipe->tx_path, O_RDWR | O_CREAT | O_TRUNC, 0666)')


def assert_rv32_audio_controller_log_uses_regular_trace_source():
    rv32_runner = read_text(ROOT.parent / 'application' / 'rv32qemu' / 'sof-build-test.py')
    rv32_ac_platform = read_text(ROOT.parent / 'Misc' / 'sof_test' / 'platform' / 'rv32qemu' / 'ac_platform.c')

    require_contains(rv32_runner, 'ensure_data_file(log_fifo_path)')
    assert 'ensure_fifo(log_fifo_path)' not in rv32_runner
    assert 'if (count == 0 || (count < 0 && errno != EAGAIN &&' not in rv32_ac_platform
    require_contains(rv32_ac_platform, 'if (timeout_ms == 0u || waited_ms >= timeout_ms)')


def assert_default_rpc_endpoint_can_be_omitted_for_as_log():
    cli_common = read_text(ROOT / 'cli' / 'common' / 'src' / 'cli_common.cpp')
    as_server = read_text(ROOT / 'server' / 'as_server' / 'main.cpp')
    rv32_helper = read_text(ROOT.parent / 'application' / 'rv32qemu' / 'sof-build-test.py')

    require_contains(cli_common, 'uint16_t port = 9900;')
    require_contains(as_server, 'uint16_t port = 9900;')
    require_contains(rv32_helper, 'DEFAULT_AS_SERVER_PORT = 9900')

    as_log_section = rv32_helper.split('as_log_cmd = [', 1)[1].split(']', 1)[0]
    for forbidden in ('"--host"', '"--port"'):
        assert forbidden not in as_log_section

    server_cmd_section = rv32_helper.split('server_cmd = [', 1)[1].split(']', 1)[0]
    for forbidden in ('"--host"', '"--port"'):
        assert forbidden not in server_cmd_section


def assert_rv32_log_timeout_is_not_fatal_for_follow():
    rv32_log_device = read_text(ROOT / 'server' / 'platform' / 'simulator' / 'src' / 'rv32qemu_log_device.cpp')
    require_contains(rv32_log_device, 'isTransportReadTimeout')
    require_contains(rv32_log_device, 'chunk.bytes.clear()')
    read_chunk = rv32_log_device.split('readChunk(drivers::log::LogRawChunk& chunk', 1)[1].split('getStats', 1)[0]
    assert 'if (!status.ok()) return status;' not in read_chunk


def assert_as_config_decode_status(result, out_dir, project_name):
    decode_log = out_dir / f'{project_name}_decode.log'
    assert decode_log.exists(), f'missing as_config decode log: {decode_log}'
    if '"tplg_decoded":true' in result:
        decode_conf = out_dir / f'{project_name}_decode.conf'
        assert decode_conf.exists() and decode_conf.stat().st_size > 0, f'missing as_config decoded output: {decode_conf}'
        assert 'ALSA lib' not in read_text(decode_log)
        require_contains(read_text(decode_conf), 'SectionDAI')
        return
    require_contains(result, '"tplg_decoded":false')
    require_contains(result, 'alsatplg decode is not available for this topology')
    require_contains(read_text(decode_log), 'tplg_decode_dai')


def assert_modular_kconfig_tree():
    root_kconfig = read_text(ROOT / 'Kconfig')
    for source in [
        'source "GUI/Kconfig"',
        'source "tests/Kconfig"',
        'source "drivers/Kconfig"',
        'source "rpc/Kconfig"',
        'source "server/Kconfig"',
        'source "cli/Kconfig"',
    ]:
        require_contains(root_kconfig, source)

    driver_kconfig = read_text(ROOT / 'drivers' / 'Kconfig')
    for source in [
        'source "drivers/os/Kconfig"',
        'source "drivers/socket/Kconfig"',
        'source "drivers/audio/Kconfig"',
        'source "drivers/control/Kconfig"',
        'source "drivers/log/Kconfig"',
        'source "drivers/dump/Kconfig"',
    ]:
        require_contains(driver_kconfig, source)

    audio_kconfig = read_text(ROOT / 'drivers' / 'audio' / 'Kconfig')
    require_contains(audio_kconfig, 'config DRIVER_AUDIO')
    require_contains(audio_kconfig, 'config DRIVER_AUDIO_ALSA')
    require_contains(audio_kconfig, 'config DRIVER_AUDIO_PULSE')
    require_contains(audio_kconfig, 'config DRIVER_AUDIO_WASAPI')
    socket_kconfig = read_text(ROOT / 'drivers' / 'socket' / 'Kconfig')
    require_contains(socket_kconfig, 'config DRIVER_SOCKET_LINUX_HOST')
    require_contains(socket_kconfig, 'config DRIVER_SOCKET_WINDOWS_HOST')
    rpc_kconfig = read_text(ROOT / 'rpc' / 'Kconfig')
    require_contains(rpc_kconfig, 'config RPC_CLIENT')
    require_contains(rpc_kconfig, 'config RPC_SERVER')
    require_contains(rpc_kconfig, 'config RPC_TRANSPORT_SOCKET')
    require_contains(rpc_kconfig, 'config RPC_TRANSPORT_PIPE')
    platform_kconfig = read_text(ROOT / 'server' / 'platform' / 'Kconfig')
    require_contains(platform_kconfig, 'source "server/platform/a2/Kconfig"')
    require_contains(platform_kconfig, 'source "server/platform/simulator/Kconfig"')
    simulator_kconfig = read_text(ROOT / 'server' / 'platform' / 'simulator' / 'Kconfig')
    require_contains(simulator_kconfig, 'config PLATFORM_SIMULATOR')


def assert_driver_cmake_tree():
    for module in [
        'audio',
        'control',
        'dump',
        'dynlib',
        'filesystem',
        'log',
        'os',
        'pipe',
        'socket',
        'datalink',
    ]:
        assert (ROOT / 'drivers' / module / 'CMakeLists.txt').exists()
        assert not (ROOT / 'drivers' / module / 'src' / 'CMakeLists.txt').exists()


def assert_a2_config():
    dot_config = read_text(LINUX_BUILD_DIR / 'generated' / '.config')
    autoconfig = read_text(LINUX_BUILD_DIR / 'generated' / 'include' / 'autoconfig.h')
    require_contains(dot_config, 'CONFIG_TARGET_PLATFORM_A2=y')
    require_contains(dot_config, 'CONFIG_SERVER=y')
    require_contains(dot_config, '# CONFIG_GUI_BACKEND is not set')
    require_contains(autoconfig, '#define CONFIG_TARGET_PLATFORM_A2 1')
    assert 'CONFIG_TOOL_OS_' not in dot_config
    assert 'CONFIG_TOOLCHAIN_' not in dot_config
    return dot_config, autoconfig


def exercise_kconfig_targets():
    help_text = check_output(['cmake', '--build', str(LINUX_BUILD_DIR), '--target', 'help'])
    for target in [
        'menuconfig',
        'olddefconfig',
        'savedefconfig',
        'alldefconfig',
        'overrideconfig',
        'as_server_minimal_defconfig',
        'as_config_defconfig',
        'driver_interface_tests_defconfig',
        'gui_backend_defconfig',
        'rpc_socket_defconfig',
        'rpc_pipe_defconfig',
        'a2_defconfig',
        'simulator_defconfig',
    ]:
        require_contains(help_text, target)

    cmake_target('olddefconfig')
    assert_a2_config()

    cmake_target('savedefconfig')
    saved_defconfig = read_text(LINUX_BUILD_DIR / 'defconfig')
    require_contains(saved_defconfig, '# CONFIG_GUI_BACKEND is not set')
    require_contains(saved_defconfig, '# CONFIG_BUILD_TESTS is not set')
    require_contains(saved_defconfig, 'CONFIG_SERVER=y')

    cmake_target('simulator_defconfig')
    cmake_target('genconfig')
    simulator_config = read_text(LINUX_BUILD_DIR / 'generated' / '.config')
    simulator_header = read_text(LINUX_BUILD_DIR / 'generated' / 'include' / 'autoconfig.h')
    require_contains(simulator_config, 'CONFIG_TARGET_PLATFORM_SIMULATOR=y')
    require_contains(simulator_header, '#define CONFIG_TARGET_PLATFORM_SIMULATOR 1')

    override_config = LINUX_BUILD_DIR / 'override.config'
    override_config.write_text(
        '# CONFIG_SERVER is not set\n'
        'CONFIG_BUILD_TESTS=y\n',
        encoding='utf-8',
    )
    cmake_target('overrideconfig')
    cmake_target('genconfig')
    overridden = read_text(LINUX_BUILD_DIR / 'generated' / '.config')
    require_contains(overridden, '# CONFIG_SERVER is not set')
    require_contains(overridden, 'CONFIG_BUILD_TESTS=y')

    cmake_target('alldefconfig')
    cmake_target('genconfig')
    all_default = read_text(LINUX_BUILD_DIR / 'generated' / '.config')
    require_contains(all_default, 'CONFIG_TARGET_PLATFORM_A2=y')
    require_contains(all_default, 'CONFIG_GUI_BACKEND=y')
    require_contains(all_default, '# CONFIG_SERVER is not set')


def main():
    assert not (ROOT / 'config').exists(), 'top-level config/ must be folded into configs/'
    assert not (ROOT / 'GUI' / 'frontend' / 'config').exists(), 'built-in catalog must live under configs/'
    assert (A2_PLATFORM_DIR / 'a2_defconfig').exists(), 'missing a2 platform defconfig'
    assert A2_PROJECT_JSON.exists(), 'missing a2 platform JSON config'
    assert (SIMULATOR_PLATFORM_DIR / 'simulator_defconfig').exists(), 'missing simulator platform defconfig'
    assert SIMULATOR_PROJECT_JSON.exists(), 'missing simulator platform JSON config'
    assert BUILTIN_ALGORITHM_JSON.exists(), 'missing built-in algorithm catalog under configs/'

    assert_modular_kconfig_tree()
    assert_driver_cmake_tree()
    assert_datalink_driver_naming()
    assert_as_log_embeds_sof_decoder()
    assert_sof_test_ac_run_is_platform_neutral()
    assert_as_log_cli_is_transport_neutral()
    assert_framework_build_config_is_modular()
    assert_audio_controller_transport_channels_are_layered()
    assert_simulator_audio_transport_channels_are_stream_scoped()
    assert_audio_cli_lets_server_select_default_driver()
    assert_sof_logger_follow_ignores_decoder_footer()
    assert_sof_test_ac_run_uses_getopt_long()
    assert_rv32_log_datalink_files_are_session_scoped()
    assert_rv32_audio_controller_log_uses_regular_trace_source()
    assert_default_rpc_endpoint_can_be_omitted_for_as_log()
    assert_rv32_log_timeout_is_not_fatal_for_follow()

    if LINUX_BUILD_DIR.exists():
        shutil.rmtree(LINUX_BUILD_DIR)
    run([str(BUILD_ALL), 'linux', 'a2'])
    expected = [
        LINUX_BUILD_DIR / 'generated' / '.config',
        LINUX_BUILD_DIR / 'generated' / 'include' / 'autoconfig.h',
        LINUX_BUILD_DIR / 'CMakeCache.txt',
        LINUX_BUILD_DIR / 'as_server',
    ]
    for path in expected:
        assert path.exists(), f'missing build output: {path}'

    assert_a2_config()
    exercise_kconfig_targets()
    run([str(BUILD_ALL), 'linux', 'a2'])
    assert_a2_config()

    version = check_output([str(LINUX_BUILD_DIR / 'as_server'), '--version'])
    assert 'Audio Studio as_server initial linux/a2' in version
    health = check_output([str(LINUX_BUILD_DIR / 'as_server'), '--health'])
    assert '"tool_os":"linux"' in health
    assert '"platform":"a2"' in health

    dry_run = run(
        [str(BUILD_ALL), '--dry-run', 'windows', 'a2'],
        text=True,
        stdout=subprocess.PIPE,
    ).stdout
    assert 'PLATFORM_CONFIG=' in dry_run and 'configs/platform/a2/a2_defconfig' in dry_run
    assert 'OS=windows' in dry_run
    assert 'windows-mingw.cmake' in dry_run

    if DRIVER_BUILD_DIR.exists():
        shutil.rmtree(DRIVER_BUILD_DIR)
    run([str(BUILD_ALL), '--profile', 'driver_interface_tests', 'linux', 'a2'])
    driver_config = read_text(DRIVER_BUILD_DIR / 'generated' / '.config')
    driver_header = read_text(DRIVER_BUILD_DIR / 'generated' / 'include' / 'autoconfig.h')
    require_contains(driver_config, 'CONFIG_DRIVER_INTERFACE_TESTS=y')
    require_contains(driver_config, 'CONFIG_SERVER_FRAMEWORK_TESTS=y')
    require_contains(driver_config, 'CONFIG_RPC=y')
    require_contains(driver_config, 'CONFIG_RPC_CLIENT=y')
    require_contains(driver_config, 'CONFIG_RPC_SERVER=y')
    require_contains(driver_config, 'CONFIG_RPC_TRANSPORT_SOCKET=y')
    require_contains(driver_config, 'CONFIG_RPC_TRANSPORT_PIPE=y')
    require_contains(driver_config, 'CONFIG_FRAMEWORK_AUDIO=y')
    require_contains(driver_config, 'CONFIG_DRIVER_SOCKET_LINUX_HOST=y')
    require_contains(driver_config, 'CONFIG_DRIVER_AUDIO_ALSA=y')
    require_contains(driver_config, 'CONFIG_DRIVER_AUDIO_PULSE=y')
    require_contains(driver_header, '#define CONFIG_DRIVER_DUMP_LINUX_HOST 1')
    assert (DRIVER_BUILD_DIR / 'audio_studio_server_tests').exists()
    assert (DRIVER_BUILD_DIR / 'audio_studio_driver_interface_tests').exists()
    run(['ctest', '--test-dir', str(DRIVER_BUILD_DIR), '--output-on-failure'])
    rpc_health = check_output([
        str(DRIVER_BUILD_DIR / 'as_server'),
        '--rpc-once',
        '{"jsonrpc":"2.0","id":1,"method":"server.health"}',
    ])
    require_contains(rpc_health, '"jsonrpc":"2.0"')
    require_contains(rpc_health, '"tool_os":"linux"')

    if AS_CONFIG_BUILD_DIR.exists():
        shutil.rmtree(AS_CONFIG_BUILD_DIR)
    assert os.access(PREBUILT_ALSATPLG, os.X_OK), f'missing executable prebuilt alsatplg: {PREBUILT_ALSATPLG}'
    run([str(BUILD_ALL), '--profile', 'as_config', 'linux', 'a2'])
    as_config_config = read_text(AS_CONFIG_BUILD_DIR / 'generated' / '.config')
    require_contains(as_config_config, 'CONFIG_FRAMEWORK_CONFIG=y')
    require_contains(as_config_config, 'CONFIG_DRIVER_FILESYSTEM_LINUX_HOST=y')
    require_contains(as_config_config, 'CONFIG_DRIVER_DYNLIB_LINUX_HOST=y')
    assert (AS_CONFIG_BUILD_DIR / 'as_config').exists()
    as_config_out = ROOT / 'out' / 'as-config-build-test'
    if as_config_out.exists():
        shutil.rmtree(as_config_out)
    as_config_result = check_output([
        str(AS_CONFIG_BUILD_DIR / 'as_config'),
        '--input', str(A2_PROJECT_JSON),
        '--out-dir', str(as_config_out),
        '--project-name', 'a2_test',
    ])
    require_contains(as_config_result, '"runtime_control_count":19')
    require_contains(as_config_result, '"pipeline_count":3')
    require_contains(as_config_result, '"tplg_built":true')
    assert_as_config_decode_status(as_config_result, as_config_out, 'a2_test')
    require_contains(as_config_result, '"preset_count":2')
    for path in [
        as_config_out / 'a2_test.conf',
        as_config_out / 'a2_test.tplg',
        as_config_out / 'a2_test_private.bin',
        as_config_out / 'include' / 'as_config_ids.h',
        as_config_out / 'include' / 'as_tplg_private.h',
        as_config_out / 'include' / 'as_preset_ids.h',
        as_config_out / 'a2_test_controls.csv',
        as_config_out / 'a2_test_compile_report.json',
    ]:
        assert path.exists() and path.stat().st_size > 0, f'missing as_config output: {path}'
    for path in [
        as_config_out / 'a2_test_alsatplg.log',
        as_config_out / 'a2_test_decode.log',
    ]:
        assert path.exists(), f'missing as_config log: {path}'
    assert 'ALSA lib' not in read_text(as_config_out / 'a2_test_alsatplg.log')
    conf_text = read_text(as_config_out / 'a2_test.conf')
    require_contains(conf_text, 'SOF_TKN_DAI_TYPE "FILE_IO"')
    assert 'SOF_TKN_DAI_TYPE "VSI_TDM"' not in conf_text
    require_contains(conf_text, 'SectionControlBytes.')
    require_contains(conf_text, 'bytes [')
    require_contains(conf_text, 'SOF_TKN_PROCESS_TYPE "CHAN_REMAP"')
    require_contains(conf_text, 'SOF_TKN_PROCESS_TYPE "DELAY_LINE"')
    require_contains(conf_text, 'SOF_TKN_PROCESS_TYPE "FADER_BALANCE"')
    require_contains(conf_text, 'SOF_TKN_PROCESS_TYPE "DSP_FILTER"')

    if AUDIO_CONTROLLER_BUILD_DIR.exists():
        shutil.rmtree(AUDIO_CONTROLLER_BUILD_DIR)
    run([
        'cmake',
        '-S', str(ROOT / 'audio_controller'),
        '-B', str(AUDIO_CONTROLLER_BUILD_DIR),
    ])
    run(['cmake', '--build', str(AUDIO_CONTROLLER_BUILD_DIR)])
    run(['ctest', '--test-dir', str(AUDIO_CONTROLLER_BUILD_DIR), '--output-on-failure'])
    audio_controller_lib = AUDIO_CONTROLLER_BUILD_DIR / 'libaudio_controller.a'
    assert audio_controller_lib.exists(), f'missing audio_controller library: {audio_controller_lib}'
    audio_controller_header = ROOT / 'audio_controller' / 'include' / 'audio_controller.h'
    assert audio_controller_header.exists(), f'missing audio_controller public header: {audio_controller_header}'

    if AUDIO_CONTROLLER_PROFILE_BUILD_DIR.exists():
        shutil.rmtree(AUDIO_CONTROLLER_PROFILE_BUILD_DIR)
    run([str(BUILD_ALL), '--profile', 'audio_controller', 'linux', 'a2'])
    run(['ctest', '--test-dir', str(AUDIO_CONTROLLER_PROFILE_BUILD_DIR), '--output-on-failure'])
    profile_audio_controller_lib = AUDIO_CONTROLLER_PROFILE_BUILD_DIR / 'libaudio_controller.a'
    assert profile_audio_controller_lib.exists(), f'missing audio_controller profile library: {profile_audio_controller_lib}'
    ids_header = read_text(as_config_out / 'include' / 'as_config_ids.h')
    require_contains(ids_header, 'AS_MODULE_TYPE_RATE_SRC')
    assert 'AS_MODULE_TYPE_SERVICE_ASRC' not in ids_header
    require_contains(ids_header, '#define AS_PARAM_GAIN_VOLUME_VOLUME_DB 0x7B6FD765u')
    require_contains(ids_header, 'AS_CONTROL_PLAYBACK_MAIN_VOLUME_VOLUME_DB')
    private_payload = (as_config_out / 'a2_test_private.bin').read_bytes()
    assert b'as-builtin-gain-volume-runtime-json-v1' in private_payload
    assert b'as-builtin-gain-volume-preset-json-v1' in private_payload
    assert b'"pipelines"' in private_payload
    assert b'"dai_id":"FILE_IO_PLAYBACK_DAI0"' in private_payload
    assert b'"tdm_slots":2' in private_payload
    assert b'"config_format":"sof-ipc3-bytes-v1"' in private_payload
    assert b'"codec_format"' not in private_payload
    assert b'"config_format"' in private_payload
    a2_json_text = read_text(A2_PROJECT_JSON)
    simulator_json_text = read_text(SIMULATOR_PROJECT_JSON)
    builtin_json_text = read_text(BUILTIN_ALGORITHM_JSON)
    assert 'param_encoding' not in a2_json_text
    require_contains(a2_json_text, '"path": "configs/built-in-algorithm.json"')
    assert '"type_id": "filter.channel_remap"' not in a2_json_text
    assert '"type_id": "mix.fader_balance"' not in a2_json_text
    assert '"type_id": "filter.dsp_filter"' not in a2_json_text
    require_contains(builtin_json_text, '"schema_version": "2.0.0"')
    require_contains(simulator_json_text, '"path": "configs/built-in-algorithm.json"')
    require_contains(builtin_json_text, '"type_id": "filter.channel_remap"')
    require_contains(builtin_json_text, '"param_id": "layout"')
    require_contains(builtin_json_text, '"type_id": "filter.dsp_filter"')
    require_contains(builtin_json_text, '"param_id": "filter_preset"')

    as_config_no_tplg_out = ROOT / 'out' / 'as-config-no-tplg-test'
    if as_config_no_tplg_out.exists():
        shutil.rmtree(as_config_no_tplg_out)
    no_tplg_env = os.environ.copy()
    no_tplg_env.pop('SOF_UUID_REGISTRY', None)
    with tempfile.TemporaryDirectory(prefix='as-config-no-tplg-cwd-') as no_tplg_cwd:
        no_tplg_result = check_output([
            str(AS_CONFIG_BUILD_DIR / 'as_config'),
            '--input', str(A2_PROJECT_JSON),
            '--out-dir', str(as_config_no_tplg_out),
            '--project-name', 'a2_no_tplg_test',
            '--no-tplg',
        ], cwd=no_tplg_cwd, env=no_tplg_env)
    require_contains(no_tplg_result, '"runtime_control_count":19')
    require_contains(no_tplg_result, '"pipeline_count":3')
    require_contains(no_tplg_result, '"tplg_built":false')
    for path in [
        as_config_no_tplg_out / 'a2_no_tplg_test.conf',
        as_config_no_tplg_out / 'a2_no_tplg_test_private.bin',
        as_config_no_tplg_out / 'include' / 'as_config_ids.h',
        as_config_no_tplg_out / 'include' / 'as_tplg_private.h',
        as_config_no_tplg_out / 'include' / 'as_preset_ids.h',
        as_config_no_tplg_out / 'a2_no_tplg_test_controls.csv',
        as_config_no_tplg_out / 'a2_no_tplg_test_compile_report.json',
    ]:
        assert path.exists() and path.stat().st_size > 0, f'missing no-tplg as_config output: {path}'
    for path in [
        as_config_no_tplg_out / 'a2_no_tplg_test.tplg',
        as_config_no_tplg_out / 'a2_no_tplg_test_alsatplg.log',
        as_config_no_tplg_out / 'a2_no_tplg_test_decode.conf',
        as_config_no_tplg_out / 'a2_no_tplg_test_decode.log',
    ]:
        assert not path.exists(), f'unexpected no-tplg as_config output: {path}'

    builtin_module_config_plugin = AS_CONFIG_BUILD_DIR / 'plugins' / 'builtin_module_configs' / 'libaudio_studio_builtin_module_configs.so'
    assert builtin_module_config_plugin.exists(), f'missing builtin module config plugin: {builtin_module_config_plugin}'
    builtin_config_out = ROOT / 'out' / 'as-config-builtin-module-config-test'
    if builtin_config_out.exists():
        shutil.rmtree(builtin_config_out)
    check_output([
        str(AS_CONFIG_BUILD_DIR / 'as_config'),
        '--input', str(A2_PROJECT_JSON),
        '--out-dir', str(builtin_config_out),
        '--project-name', 'a2_builtin_module_config_test',
        '--plugin', str(builtin_module_config_plugin),
    ])
    builtin_private_payload = (builtin_config_out / 'a2_builtin_module_config_test_private.bin').read_bytes()
    assert b'as-builtin-gain-volume-runtime-json-v1' in builtin_private_payload
    builtin_report = read_text(builtin_config_out / 'a2_builtin_module_config_test_compile_report.json')
    require_contains(builtin_report, 'as.builtin.gain-volume-module-config-v1')

    if MODULE_CONFIG_EXAMPLE_BUILD_DIR.exists():
        shutil.rmtree(MODULE_CONFIG_EXAMPLE_BUILD_DIR)
    run([
        'cmake',
        '-S', str(ROOT / 'plugins' / 'module_config_sdk' / 'examples' / 'third_party_module'),
        '-B', str(MODULE_CONFIG_EXAMPLE_BUILD_DIR),
    ])
    run(['cmake', '--build', str(MODULE_CONFIG_EXAMPLE_BUILD_DIR)])
    third_party_module_config_plugin = MODULE_CONFIG_EXAMPLE_BUILD_DIR / 'libas_third_party_module_config.so'
    assert third_party_module_config_plugin.exists(), f'missing third-party module config plugin: {third_party_module_config_plugin}'
    plugin_project = ROOT / 'out' / 'module-config-plugin-project.json'
    write_module_config_plugin_project(plugin_project)
    plugin_config_out = ROOT / 'out' / 'as-config-module-config-plugin-test'
    if plugin_config_out.exists():
        shutil.rmtree(plugin_config_out)
    plugin_compile = check_output([
        str(AS_CONFIG_BUILD_DIR / 'as_config'),
        '--input', str(plugin_project),
        '--out-dir', str(plugin_config_out),
        '--project-name', 'module_config_plugin_test',
        '--no-tplg',
        '--plugin', str(third_party_module_config_plugin),
    ])
    require_contains(plugin_compile, '"runtime_control_count":1')
    require_contains(plugin_compile, '"preset_count":1')
    plugin_private_payload = (plugin_config_out / 'module_config_plugin_test_private.bin').read_bytes()
    assert b'example-third-party-runtime-v1' in plugin_private_payload
    assert b'example-third-party-preset-v1' in plugin_private_payload

    rpc_config_out = ROOT / 'out' / 'as-config-rpc-test'
    if rpc_config_out.exists():
        shutil.rmtree(rpc_config_out)
    rpc_compile = check_output([
        str(AS_CONFIG_BUILD_DIR / 'as_server'),
        '--rpc-once',
        '{"jsonrpc":"2.0","id":2,"method":"config.compile","params":{'
        f'"input_path":"{A2_PROJECT_JSON}",'
        f'"output_dir":"{rpc_config_out}",'
        '"project_name":"a2_rpc_test","build_tplg":true}}',
    ])
    require_contains(rpc_compile, '"jsonrpc":"2.0"')
    require_contains(rpc_compile, '"runtime_control_count":19')
    require_contains(rpc_compile, '"pipeline_count":3')
    assert_as_config_decode_status(rpc_compile, rpc_config_out, 'a2_rpc_test')
    assert (rpc_config_out / 'a2_rpc_test.tplg').exists()
    assert 'ALSA lib' not in read_text(rpc_config_out / 'a2_rpc_test_alsatplg.log')

    if GUI_BACKEND_BUILD_DIR.exists():
        shutil.rmtree(GUI_BACKEND_BUILD_DIR)
    run([str(BUILD_ALL), '--profile', 'gui_backend', '-r', 'linux', 'a2'])
    gui_backend_config = read_text(GUI_BACKEND_BUILD_DIR / 'generated' / '.config')
    gui_backend_header = read_text(GUI_BACKEND_BUILD_DIR / 'generated' / 'include' / 'autoconfig.h')
    require_contains(gui_backend_config, 'CONFIG_GUI_BACKEND=y')
    require_contains(gui_backend_config, '# CONFIG_SERVER is not set')
    require_contains(gui_backend_header, '#define CONFIG_GUI_BACKEND 1')
    assert (GUI_BACKEND_BUILD_DIR / 'audio_studio_server').exists()
    assert (GUI_BACKEND_BUILD_DIR / 'audio_studio_backend_tests').exists()
    run(['ctest', '--test-dir', str(GUI_BACKEND_BUILD_DIR), '--output-on-failure'])

    if RPC_SOCKET_BUILD_DIR.exists():
        shutil.rmtree(RPC_SOCKET_BUILD_DIR)
    run([str(BUILD_ALL), '--profile', 'rpc_socket', 'linux', 'a2'])
    rpc_socket_config = read_text(RPC_SOCKET_BUILD_DIR / 'generated' / '.config')
    require_contains(rpc_socket_config, 'CONFIG_SERVER=y')
    require_contains(rpc_socket_config, 'CONFIG_CLI=y')
    require_contains(rpc_socket_config, 'CONFIG_RPC_TRANSPORT_SOCKET=y')
    assert 'CONFIG_RPC_TRANSPORT_PIPE=y' not in rpc_socket_config
    assert (RPC_SOCKET_BUILD_DIR / 'as_server').exists()
    assert (RPC_SOCKET_BUILD_DIR / 'as_control').exists()
    run(['ctest', '--test-dir', str(RPC_SOCKET_BUILD_DIR), '--output-on-failure'])
    socket_port = find_free_port()
    rpc_play_wav = ROOT / 'out' / 'rpc-play-smoke.wav'
    rpc_socket_record_wav = ROOT / 'out' / 'rpc-socket-record-smoke.wav'
    write_test_wav(rpc_play_wav)
    if rpc_socket_record_wav.exists():
        rpc_socket_record_wav.unlink()
    socket_server = subprocess.Popen(
        [str(RPC_SOCKET_BUILD_DIR / 'as_server'), '--rpc', '--host', '127.0.0.1', '--port', socket_port, '--max-requests', '20'],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        socket_cli = retry_check_output([
            str(RPC_SOCKET_BUILD_DIR / 'as_control'),
            '--host', '127.0.0.1',
            '--port', socket_port,
            '--action', 'get-health',
        ])
        require_contains(socket_cli, '"ok":true')
        require_contains(socket_cli, '"platform":"a2"')
        socket_log = retry_check_output([
            str(RPC_SOCKET_BUILD_DIR / 'as_log'),
            '--host', '127.0.0.1',
            '--port', socket_port,
            '--level', 'info',
            '--count', '2',
            '--no-color',
        ])
        require_contains(socket_log, '[INF] [FW] audio controller log online')
        require_contains(socket_log, '[WRN] [FW] transport channel waiting for host')
        socket_play = retry_check_output([
            str(RPC_SOCKET_BUILD_DIR / 'as_play'),
            '--host', '127.0.0.1',
            '--port', socket_port,
            '--device', 'null',
            '--file', str(rpc_play_wav),
        ])
        require_contains(socket_play, '"ok":true')
        require_contains(socket_play, f':{socket_port}/streams/')
        require_contains(socket_play, '"played_bytes":9600')
        socket_record = retry_check_output([
            str(RPC_SOCKET_BUILD_DIR / 'as_record'),
            '--host', '127.0.0.1',
            '--port', socket_port,
            '--device', 'null',
            '--output', str(rpc_socket_record_wav),
            '--duration-ms', '50',
            '--chunk-bytes', '4096',
        ])
        require_contains(socket_record, '"ok":true')
        require_contains(socket_record, '"recorded_bytes":9600')
        assert_wav(rpc_socket_record_wav, 2400)
        missing_device = 'audio_studio_missing_playback_device'
        failed_play = subprocess.run(
            [
                str(RPC_SOCKET_BUILD_DIR / 'as_play'),
                '--host', '127.0.0.1',
                '--port', socket_port,
                '--device', missing_device,
                '--file', str(rpc_play_wav),
            ],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert failed_play.returncode != 0
        failed_output = failed_play.stdout + failed_play.stderr
        require_contains(failed_output, 'failed to create playback audio driver')
        require_contains(failed_output, f'snd_pcm_open playback {missing_device} failed')
        assert socket_server.wait(timeout=5) == 0
    finally:
        if socket_server.poll() is None:
            socket_server.terminate()

    if RPC_SOCKET_SIMULATOR_BUILD_DIR.exists():
        shutil.rmtree(RPC_SOCKET_SIMULATOR_BUILD_DIR)
    run([str(BUILD_ALL), '--profile', 'rpc_socket', 'linux', 'simulator'])
    rpc_socket_sim_config = read_text(RPC_SOCKET_SIMULATOR_BUILD_DIR / 'generated' / '.config')
    require_contains(rpc_socket_sim_config, 'CONFIG_TARGET_PLATFORM_SIMULATOR=y')
    require_contains(rpc_socket_sim_config, 'CONFIG_PLATFORM_SIMULATOR=y')
    assert (RPC_SOCKET_SIMULATOR_BUILD_DIR / 'as_server').exists()
    assert (RPC_SOCKET_SIMULATOR_BUILD_DIR / 'as_log').exists()
    run(['ctest', '--test-dir', str(RPC_SOCKET_SIMULATOR_BUILD_DIR), '--output-on-failure'])
    simulator_port = find_free_port()
    simulator_server = subprocess.Popen(
        [
            str(RPC_SOCKET_SIMULATOR_BUILD_DIR / 'as_server'),
            '--rpc',
            '--host', '127.0.0.1',
            '--port', simulator_port,
            '--max-requests', '5',
            '--log-driver-factory', 'rv32qemu-simulator',
        ],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        simulator_log = retry_check_output([
            str(RPC_SOCKET_SIMULATOR_BUILD_DIR / 'as_log'),
            '--host', '127.0.0.1',
            '--port', simulator_port,
            '--level', 'debug',
            '--count', '2',
            '--no-color',
        ])
        require_contains(simulator_log, '[INF] [FW] rv32qemu audio controller log channel open')
        require_contains(simulator_log, '[DBG] [TRP] transport manager log read request')
        assert simulator_server.wait(timeout=5) == 0
    finally:
        if simulator_server.poll() is None:
            simulator_server.terminate()

    if shutil.which('pactl'):
        try:
            subprocess.run(['pactl', 'info'], cwd=str(ROOT), check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        except subprocess.CalledProcessError:
            print('build-system.test: skipped PulseAudio RPC smoke, pactl info failed', file=sys.stderr)
        else:
            pulse_port = find_free_port()
            rpc_pulse_record_wav = ROOT / 'out' / 'rpc-pulse-record-smoke.wav'
            if rpc_pulse_record_wav.exists():
                rpc_pulse_record_wav.unlink()
            pulse_server = subprocess.Popen(
                [str(RPC_SOCKET_BUILD_DIR / 'as_server'), '--rpc', '--host', '127.0.0.1', '--port', pulse_port, '--max-requests', '13'],
                cwd=str(ROOT),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            try:
                pulse_play = retry_check_output([
                    str(RPC_SOCKET_BUILD_DIR / 'as_play'),
                    '--host', '127.0.0.1',
                    '--port', pulse_port,
                    '--driver-factory', 'pulse',
                    '--device', 'default',
                    '--file', str(rpc_play_wav),
                ])
                require_contains(pulse_play, '"ok":true')
                require_contains(pulse_play, '"played_bytes":9600')
                pulse_record = retry_check_output([
                    str(RPC_SOCKET_BUILD_DIR / 'as_record'),
                    '--host', '127.0.0.1',
                    '--port', pulse_port,
                    '--driver-factory', 'pulse',
                    '--device', 'default',
                    '--output', str(rpc_pulse_record_wav),
                    '--duration-ms', '50',
                    '--chunk-bytes', '4096',
                ])
                require_contains(pulse_record, '"ok":true')
                require_contains(pulse_record, '"recorded_bytes":9600')
                assert_wav(rpc_pulse_record_wav, 2400)
                assert pulse_server.wait(timeout=5) == 0
            finally:
                if pulse_server.poll() is None:
                    pulse_server.terminate()
    else:
        print('build-system.test: skipped PulseAudio RPC smoke, missing pactl', file=sys.stderr)

    if RPC_PIPE_BUILD_DIR.exists():
        shutil.rmtree(RPC_PIPE_BUILD_DIR)
    run([str(BUILD_ALL), '--profile', 'rpc_pipe', 'linux', 'a2'])
    rpc_pipe_config = read_text(RPC_PIPE_BUILD_DIR / 'generated' / '.config')
    require_contains(rpc_pipe_config, 'CONFIG_SERVER=y')
    require_contains(rpc_pipe_config, 'CONFIG_CLI=y')
    require_contains(rpc_pipe_config, 'CONFIG_RPC_TRANSPORT_PIPE=y')
    assert 'CONFIG_RPC_TRANSPORT_SOCKET=y' not in rpc_pipe_config
    assert (RPC_PIPE_BUILD_DIR / 'as_server').exists()
    assert (RPC_PIPE_BUILD_DIR / 'as_control').exists()
    run(['ctest', '--test-dir', str(RPC_PIPE_BUILD_DIR), '--output-on-failure'])
    request_pipe = ROOT / 'out' / 'rpc-test.req'
    response_pipe = ROOT / 'out' / 'rpc-test.rsp'
    rpc_pipe_record_wav = ROOT / 'out' / 'rpc-pipe-record-smoke.wav'
    for path in [request_pipe, response_pipe, rpc_pipe_record_wav]:
        if path.exists():
            path.unlink()
    pipe_server = subprocess.Popen(
        [str(RPC_PIPE_BUILD_DIR / 'as_server'), '--rpc', 'pipe', str(request_pipe), str(response_pipe), '--max-requests', '19'],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        wait_for_paths([request_pipe, response_pipe], pipe_server)
        pipe_cli = retry_check_output([
            str(RPC_PIPE_BUILD_DIR / 'as_control'),
            '--rpc', 'pipe',
            '--request-pipe', str(request_pipe),
            '--response-pipe', str(response_pipe),
            '--action', 'get-health',
        ])
        require_contains(pipe_cli, '"ok":true')
        require_contains(pipe_cli, '"platform":"a2"')
        pipe_log = retry_check_output([
            str(RPC_PIPE_BUILD_DIR / 'as_log'),
            '--rpc', 'pipe',
            '--request-pipe', str(request_pipe),
            '--response-pipe', str(response_pipe),
            '--level', 'warning',
            '--count', '1',
            '--no-color',
        ])
        require_contains(pipe_log, '[WRN] [FW] transport channel waiting for host')
        pipe_play = retry_check_output([
            str(RPC_PIPE_BUILD_DIR / 'as_play'),
            '--rpc', 'pipe',
            '--request-pipe', str(request_pipe),
            '--response-pipe', str(response_pipe),
            '--device', 'null',
            '--file', str(rpc_play_wav),
        ])
        require_contains(pipe_play, '"ok":true')
        require_contains(pipe_play, '"played_bytes":9600')
        pipe_record = retry_check_output([
            str(RPC_PIPE_BUILD_DIR / 'as_record'),
            '--rpc', 'pipe',
            '--request-pipe', str(request_pipe),
            '--response-pipe', str(response_pipe),
            '--device', 'null',
            '--output', str(rpc_pipe_record_wav),
            '--duration-ms', '50',
            '--chunk-bytes', '4096',
        ])
        require_contains(pipe_record, '"ok":true')
        require_contains(pipe_record, '"recorded_bytes":9600')
        assert_wav(rpc_pipe_record_wav, 2400)
        assert pipe_server.wait(timeout=5) == 0
    finally:
        if pipe_server.poll() is None:
            pipe_server.terminate()
        for path in [request_pipe, response_pipe]:
            if path.exists():
                path.unlink()

    if shutil.which('x86_64-w64-mingw32-g++-posix'):
        if WINDOWS_BUILD_DIR.exists():
            shutil.rmtree(WINDOWS_BUILD_DIR)
        run([str(BUILD_ALL), 'windows', 'a2'])
        assert (WINDOWS_BUILD_DIR / 'as_server.exe').exists()
    else:
        print('build-system.test: skipped windows compile, missing x86_64-w64-mingw32-g++-posix', file=sys.stderr)

    if shutil.which('x86_64-w64-mingw32-g++-posix') and shutil.which('wine64'):
        if WINDOWS_RPC_SOCKET_BUILD_DIR.exists():
            shutil.rmtree(WINDOWS_RPC_SOCKET_BUILD_DIR)
        run([str(BUILD_ALL), '--profile', 'rpc_socket', 'windows', 'a2'])
        windows_rpc_config = read_text(WINDOWS_RPC_SOCKET_BUILD_DIR / 'generated' / '.config')
        require_contains(windows_rpc_config, 'CONFIG_DRIVER_SOCKET_WINDOWS_HOST=y')
        require_contains(windows_rpc_config, 'CONFIG_DRIVER_AUDIO_WASAPI=y')
        assert 'CONFIG_DRIVER_SOCKET_LINUX_HOST=y' not in windows_rpc_config
        assert 'CONFIG_DRIVER_AUDIO_ALSA=y' not in windows_rpc_config
        for exe in ['as_server.exe', 'as_control.exe', 'as_play.exe', 'as_record.exe', 'as_log.exe', 'as_dump.exe']:
            assert (WINDOWS_RPC_SOCKET_BUILD_DIR / exe).exists()
        windows_version = wine_check_output([WINDOWS_RPC_SOCKET_BUILD_DIR / 'as_server.exe', '--version'])
        require_contains(windows_version, 'Audio Studio as_server initial windows/a2')
        windows_health = wine_check_output([WINDOWS_RPC_SOCKET_BUILD_DIR / 'as_server.exe', '--health'])
        require_contains(windows_health, '"tool_os":"windows"')
        wine_check_output([WINDOWS_RPC_SOCKET_BUILD_DIR / 'audio_studio_cli_tests.exe'])

        windows_port = find_free_port()
        windows_record_wav = ROOT / 'out' / 'windows-wasapi-record-smoke.wav'
        if windows_record_wav.exists():
            windows_record_wav.unlink()
        windows_server = subprocess.Popen(
            ['wine64', str(WINDOWS_RPC_SOCKET_BUILD_DIR / 'as_server.exe'),
             '--rpc', '--host', '127.0.0.1', '--port', windows_port, '--max-requests', '14'],
            cwd=str(ROOT),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            windows_cli = retry_wine_check_output([
                WINDOWS_RPC_SOCKET_BUILD_DIR / 'as_control.exe',
                '--host', '127.0.0.1',
                '--port', windows_port,
                '--action', 'get-health',
            ])
            require_contains(windows_cli, '"ok":true')
            require_contains(windows_cli, '"tool_os":"windows"')
            windows_play = retry_wine_check_output([
                WINDOWS_RPC_SOCKET_BUILD_DIR / 'as_play.exe',
                '--host', '127.0.0.1',
                '--port', windows_port,
                '--file', wine_path(rpc_play_wav),
            ])
            require_contains(windows_play, '"ok":true')
            require_contains(windows_play, '"played_bytes":9600')
            windows_record = retry_wine_check_output([
                WINDOWS_RPC_SOCKET_BUILD_DIR / 'as_record.exe',
                '--host', '127.0.0.1',
                '--port', windows_port,
                '--output', wine_path(windows_record_wav),
                '--duration-ms', '50',
                '--chunk-bytes', '4096',
            ])
            require_contains(windows_record, '"ok":true')
            require_contains(windows_record, '"recorded_bytes":9600')
            assert_wav(windows_record_wav, 2400)
            assert windows_server.wait(timeout=5) == 0
        finally:
            if windows_server.poll() is None:
                windows_server.terminate()
    else:
        print('build-system.test: skipped windows RPC/WASAPI smoke, missing posix MinGW or wine64', file=sys.stderr)

    print('build-system.test passed')


if __name__ == '__main__':
    main()
