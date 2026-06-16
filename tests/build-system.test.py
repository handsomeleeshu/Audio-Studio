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
RPC_SOCKET_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'rpc_socket' / 'Debug'
RPC_PIPE_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'rpc_pipe' / 'Debug'
WINDOWS_BUILD_DIR = ROOT / 'out' / 'windows' / 'a2' / 'as_server_minimal' / 'Debug'


def run(command, **kwargs):
    return subprocess.run(command, cwd=str(ROOT), check=True, **kwargs)


def check_output(command):
    return subprocess.check_output(command, cwd=str(ROOT), text=True)


def retry_check_output(command, attempts=20, delay=0.1):
    last_error = None
    for _ in range(attempts):
        try:
            return check_output(command)
        except subprocess.CalledProcessError as exc:
            last_error = exc
            time.sleep(delay)
    raise last_error


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
    require_contains(audio_kconfig, 'config DRIVER_AUDIO_LINUX_HOST')
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
    require_contains(driver_config, 'CONFIG_DRIVER_AUDIO_LINUX_HOST=y')
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
        [str(RPC_SOCKET_BUILD_DIR / 'as_server'), '--rpc', '--host', '127.0.0.1', '--port', socket_port, '--max-requests', '14'],
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
        assert socket_server.wait(timeout=5) == 0
    finally:
        if socket_server.poll() is None:
            socket_server.terminate()

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

    if shutil.which('x86_64-w64-mingw32-g++'):
        if WINDOWS_BUILD_DIR.exists():
            shutil.rmtree(WINDOWS_BUILD_DIR)
        run([str(BUILD_ALL), 'windows', 'a2'])
        assert (WINDOWS_BUILD_DIR / 'as_server.exe').exists()
    else:
        print('build-system.test: skipped windows compile, missing x86_64-w64-mingw32-g++', file=sys.stderr)

    print('build-system.test passed')


if __name__ == '__main__':
    main()
