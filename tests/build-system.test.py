#!/usr/bin/env python3
import json
import pathlib
import shutil
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / 'out' / 'test-build-system' / 'linux-gcc' / 'Debug'


def run(command):
    subprocess.run(command, cwd=str(ROOT), check=True)


def check_output(command):
    return subprocess.check_output(command, cwd=str(ROOT), text=True)


def main():
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
    run([
        str(ROOT / 'scripts' / 'build_all'),
        '--profile', 'test-build-system',
        '--platform', 'host',
        '--target-os', 'linux',
        '--toolchain', 'gcc',
        '--build-type', 'Debug',
        '--defconfig', str(ROOT / 'configs' / 'host_linux_defconfig'),
    ])
    expected = [
        BUILD_DIR / '.config',
        BUILD_DIR / 'generated' / 'autoconf.h',
        BUILD_DIR / 'generated' / 'autoconf.hpp',
        BUILD_DIR / 'generated' / 'config.cmake',
        BUILD_DIR / 'generated' / 'config.json',
    ]
    for path in expected:
        assert path.exists(), f'missing generated build config: {path}'
    config = json.loads((BUILD_DIR / 'generated' / 'config.json').read_text(encoding='utf-8'))
    assert config['CONFIG_TARGET_OS_LINUX'] is True
    assert config['CONFIG_TOOLCHAIN_GCC'] is True
    assert config['CONFIG_GUI_BACKEND'] is True
    assert config['CONFIG_SERVER'] is True
    assert config['CONFIG_RPC'] is True
    assert config['CONFIG_CLI'] is True
    assert config['CONFIG_FRAMEWORK_SESSION'] is True
    assert config['CONFIG_FRAMEWORK_CONTROL'] is True
    assert config['CONFIG_DRIVER_DUMMY'] is True
    assert (BUILD_DIR / 'CMakeCache.txt').exists()
    assert (BUILD_DIR / 'as_server').exists()
    for tool in ['as_control', 'as_play', 'as_record', 'as_log', 'as_dump']:
        assert (BUILD_DIR / tool).exists(), f'missing CLI tool: {tool}'
    run(['ctest', '--test-dir', str(BUILD_DIR), '--output-on-failure'])
    self_test = json.loads(check_output([str(BUILD_DIR / 'as_server'), '--self-test']))
    assert self_test['ok'] is True
    assert self_test['driver'] == 'dummy'
    assert self_test['commands'] == 1
    control = json.loads(check_output([str(BUILD_DIR / 'as_control'), '--target', 'dummy', '--action', 'get-health']))
    assert control['ok'] is True
    assert control['tool'] == 'as_control'
    play = json.loads(check_output([str(BUILD_DIR / 'as_play'), '--target', 'dummy', '--file', 'demo.wav']))
    assert play['ok'] is True
    assert play['tool'] == 'as_play'
    print('build-system.test passed')


if __name__ == '__main__':
    main()
