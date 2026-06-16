#!/usr/bin/env python3
import pathlib
import shutil
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_ALL = ROOT / 'scripts' / 'build_all.sh'
LINUX_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'as_server_minimal' / 'Debug'
DRIVER_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'driver_interface_tests' / 'Debug'
GUI_BACKEND_BUILD_DIR = ROOT / 'out' / 'linux' / 'a2' / 'gui_backend' / 'Release'
WINDOWS_BUILD_DIR = ROOT / 'out' / 'windows' / 'a2' / 'as_server_minimal' / 'Debug'


def run(command, **kwargs):
    return subprocess.run(command, cwd=str(ROOT), check=True, **kwargs)


def check_output(command):
    return subprocess.check_output(command, cwd=str(ROOT), text=True)


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
        'source "server/Kconfig"',
        'source "cli/Kconfig"',
    ]:
        require_contains(root_kconfig, source)

    driver_kconfig = read_text(ROOT / 'server' / 'drivers' / 'Kconfig')
    for source in [
        'source "server/drivers/os/Kconfig"',
        'source "server/drivers/socket/Kconfig"',
        'source "server/drivers/audio/Kconfig"',
        'source "server/drivers/control/Kconfig"',
        'source "server/drivers/log/Kconfig"',
        'source "server/drivers/dump/Kconfig"',
    ]:
        require_contains(driver_kconfig, source)

    audio_kconfig = read_text(ROOT / 'server' / 'drivers' / 'audio' / 'Kconfig')
    require_contains(audio_kconfig, 'config DRIVER_AUDIO')
    require_contains(audio_kconfig, 'config DRIVER_AUDIO_LINUX_HOST')


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
        assert (ROOT / 'server' / 'drivers' / module / 'CMakeLists.txt').exists()
        assert not (ROOT / 'server' / 'drivers' / module / 'src' / 'CMakeLists.txt').exists()


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
    require_contains(driver_config, 'CONFIG_DRIVER_SOCKET_LINUX_HOST=y')
    require_contains(driver_config, 'CONFIG_DRIVER_AUDIO_LINUX_HOST=y')
    require_contains(driver_header, '#define CONFIG_DRIVER_DUMP_LINUX_HOST 1')
    assert (DRIVER_BUILD_DIR / 'audio_studio_driver_interface_tests').exists()
    run(['ctest', '--test-dir', str(DRIVER_BUILD_DIR), '--output-on-failure'])

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
