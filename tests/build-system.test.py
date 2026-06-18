#!/usr/bin/env python3
import pathlib
import shutil
import socket
import subprocess
import sys
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
RPC_SOCKET_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'rpc_socket' / 'Debug'
RPC_PIPE_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'rpc_pipe' / 'Debug'
WINDOWS_BUILD_DIR = ROOT / 'out' / 'windows' / 'a2' / 'as_server_minimal' / 'Debug'
WINDOWS_RPC_SOCKET_BUILD_DIR = ROOT / 'out' / 'windows' / 'a2' / 'rpc_socket' / 'Debug'


def run(command, **kwargs):
    return subprocess.run(command, cwd=str(ROOT), check=True, **kwargs)


def check_output(command):
    return subprocess.check_output(command, cwd=str(ROOT), text=True)


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
        'transport',
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
    assert_modular_kconfig_tree()
    assert_driver_cmake_tree()

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
    assert 'PLATFORM_CONFIG=' in dry_run and 'configs/platform/a2_defconfig' in dry_run
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
        '--input', str(ROOT / 'config' / 'A2.json'),
        '--out-dir', str(as_config_out),
        '--project-name', 'a2_test',
    ])
    require_contains(as_config_result, '"runtime_control_count":29')
    require_contains(as_config_result, '"tplg_built":true')
    require_contains(as_config_result, '"tplg_decoded":true')
    require_contains(as_config_result, '"preset_count":3')
    for path in [
        as_config_out / 'a2_test.conf',
        as_config_out / 'a2_test.tplg',
        as_config_out / 'a2_test_decode.conf',
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
    assert 'ALSA lib' not in read_text(as_config_out / 'a2_test_decode.log')
    decoded_conf = read_text(as_config_out / 'a2_test_decode.conf')
    require_contains(decoded_conf, 'SectionDAI {')
    require_contains(decoded_conf, 'SectionBE {')

    if AUDIO_CONTROLLER_BUILD_DIR.exists():
        shutil.rmtree(AUDIO_CONTROLLER_BUILD_DIR)
    run([
        'cmake',
        '-S', str(ROOT / 'audio_controller'),
        '-B', str(AUDIO_CONTROLLER_BUILD_DIR),
    ])
    run(['cmake', '--build', str(AUDIO_CONTROLLER_BUILD_DIR)])
    audio_controller_cli = AUDIO_CONTROLLER_BUILD_DIR / 'audio_controller'
    assert audio_controller_cli.exists(), f'missing audio_controller CLI: {audio_controller_cli}'
    audio_controller_list = check_output([
        str(audio_controller_cli),
        '--tplg', str(as_config_out / 'a2_test.tplg'),
        '--list',
    ])
    require_contains(audio_controller_list, 'topology: ')
    require_contains(audio_controller_list, 'pipelines: 4')
    require_contains(audio_controller_list, 'widgets: ')
    require_contains(audio_controller_list, 'routes: ')
    require_contains(audio_controller_list, 'controls: 26')
    require_contains(audio_controller_list, 'PLAY_MAIN.SCHED')
    require_contains(audio_controller_list, 'PLAY_MAIN.VOL')
    require_contains(audio_controller_list, 'CAPTURE_VOICE.NS')

    if AUDIO_CONTROLLER_PROFILE_BUILD_DIR.exists():
        shutil.rmtree(AUDIO_CONTROLLER_PROFILE_BUILD_DIR)
    run([str(BUILD_ALL), '--profile', 'audio_controller', 'linux', 'a2'])
    assert (AUDIO_CONTROLLER_PROFILE_BUILD_DIR / 'audio_controller').exists()
    ids_header = read_text(as_config_out / 'include' / 'as_config_ids.h')
    require_contains(ids_header, 'AS_MODULE_TYPE_RATE_ASRC')
    assert 'AS_MODULE_TYPE_SERVICE_ASRC' not in ids_header
    require_contains(ids_header, '#define AS_PARAM_GAIN_VOLUME_VOL_DB 0x172E41DCu')
    require_contains(ids_header, '#define AS_CONTROL_PLAY_MAIN_VOL_VOL_DB 0xCD13BD21u')
    private_payload = (as_config_out / 'a2_test_private.bin').read_bytes()
    assert b'as-generic-runtime-json-v1' in private_payload
    assert b'as-generic-install-json-v1' in private_payload
    assert b'as-generic-preset-json-v1' in private_payload
    assert b'"pipelines"' in private_payload
    assert b'"dai_id":"CODEC_OUT_DAI0"' in private_payload
    assert b'"tdm_slots":8' in private_payload
    assert b'"codec_format"' not in private_payload
    assert b'"config_format"' in private_payload
    a2_json_text = read_text(ROOT / 'config' / 'A2.json')
    assert 'param_encoding' not in a2_json_text
    require_contains(a2_json_text, '"type_id": "rate.asrc"')

    as_config_no_tplg_out = ROOT / 'out' / 'as-config-no-tplg-test'
    if as_config_no_tplg_out.exists():
        shutil.rmtree(as_config_no_tplg_out)
    no_tplg_result = check_output([
        str(AS_CONFIG_BUILD_DIR / 'as_config'),
        '--input', str(ROOT / 'config' / 'A2.json'),
        '--out-dir', str(as_config_no_tplg_out),
        '--project-name', 'a2_no_tplg_test',
        '--no-tplg',
    ])
    require_contains(no_tplg_result, '"runtime_control_count":29')
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
        '--input', str(ROOT / 'config' / 'A2.json'),
        '--out-dir', str(builtin_config_out),
        '--project-name', 'a2_builtin_module_config_test',
        '--plugin', str(builtin_module_config_plugin),
    ])
    builtin_private_payload = (builtin_config_out / 'a2_builtin_module_config_test_private.bin').read_bytes()
    assert b'as-builtin-gain-volume-runtime-json-v1' in builtin_private_payload
    assert b'as-builtin-eq-iir-runtime-json-v1' in builtin_private_payload
    assert b'as-builtin-rate-asrc-runtime-json-v1' in builtin_private_payload
    assert b'as-builtin-vavs-aec-runtime-json-v1' in builtin_private_payload
    builtin_report = read_text(builtin_config_out / 'a2_builtin_module_config_test_compile_report.json')
    require_contains(builtin_report, 'as.builtin.graph-copier-module-config-v1')
    require_contains(builtin_report, 'as.builtin.gain-volume-module-config-v1')
    require_contains(builtin_report, 'as.builtin.rate-asrc-module-config-v1')
    require_contains(builtin_report, 'as.builtin.vavs-dereverb-module-config-v1')

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
        f'"input_path":"{ROOT / "config" / "A2.json"}",'
        f'"output_dir":"{rpc_config_out}",'
        '"project_name":"a2_rpc_test","build_tplg":true}}',
    ])
    require_contains(rpc_compile, '"jsonrpc":"2.0"')
    require_contains(rpc_compile, '"runtime_control_count":29')
    require_contains(rpc_compile, '"tplg_decoded":true')
    assert (rpc_config_out / 'a2_rpc_test.tplg').exists()
    assert (rpc_config_out / 'a2_rpc_test_decode.conf').exists()
    assert 'ALSA lib' not in read_text(rpc_config_out / 'a2_rpc_test_alsatplg.log')
    assert 'ALSA lib' not in read_text(rpc_config_out / 'a2_rpc_test_decode.log')

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
        [str(RPC_SOCKET_BUILD_DIR / 'as_server'), '--rpc', '--host', '127.0.0.1', '--port', socket_port, '--max-requests', '15'],
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
        [str(RPC_PIPE_BUILD_DIR / 'as_server'), '--rpc', 'pipe', str(request_pipe), str(response_pipe), '--max-requests', '14'],
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
