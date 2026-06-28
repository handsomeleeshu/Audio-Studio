#!/usr/bin/env python3
import json
import importlib.util
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]
VASS_ROOT = ROOT.parent


def require_contains(text, expected):
    assert expected in text, f'missing {expected!r}'


def require_not_contains(text, forbidden):
    assert forbidden not in text, f'forbidden {forbidden!r}'


def main():
    helper = (VASS_ROOT / 'application' / 'rv32qemu' / 'sof-build-test.py').read_text(encoding='utf-8')
    require_contains(helper, 'parser.add_argument("--qemu-gdb-port"')
    require_contains(helper, 'parser.add_argument("--qemu-gdb-wait"')
    require_contains(helper, 'parser.add_argument("--gui-as-server-ready-marker"')
    require_contains(helper, 'parser.add_argument("--gui-test-list"')
    require_contains(helper, '-gdb tcp::')

    launch = json.loads((VASS_ROOT / '.vscode' / 'launch.json').read_text(encoding='utf-8'))
    configs = {item['name']: item for item in launch['configurations']}
    sim = configs['Audio Studio GUI: Simulator Keep Alive']
    assert sim['type'] == 'cppdbg'
    assert sim['request'] == 'launch'
    assert sim['program'].endswith('/application/rv32qemu/build/sof_freertos_riscv')
    assert sim['miDebuggerPath'] == '${workspaceFolder}/.vscode/riscv-gdb-wrapper.sh'
    assert sim['miDebuggerServerAddress'] == '127.0.0.1:${input:audioStudioQemuGdbPort}'
    assert sim['preLaunchTask'] == 'Audio Studio GUI: wait simulator qemu gdbstub'
    assert sim['stopAtConnect'] is True
    assert sim['launchCompleteCommand'] == 'None'
    assert 'sourceFileMap' not in sim
    assert not any('substitute-path' in item['text'] for item in sim['setupCommands'])
    assert any(item['text'] == 'tbreak main' and not item['ignoreFailures'] for item in sim['setupCommands'])
    assert 'debugServerPath' not in sim
    assert 'debugServerArgs' not in sim
    assert 'serverStarted' not in sim
    assert 'filterStdout' not in sim
    assert 'filterStderr' not in sim

    rv32_simple = configs['rv32qemu-dbg (simple test)']
    assert rv32_simple['type'] == 'cppdbg'
    assert rv32_simple['request'] == 'launch'
    assert rv32_simple['program'] == '${workspaceFolder}/application/rv32qemu/build/sof_freertos_riscv'
    assert rv32_simple['cwd'] == '${workspaceFolder}/Misc/sof_test/simple_test'
    assert rv32_simple['miDebuggerPath'] == '${workspaceFolder}/.vscode/riscv-gdb-wrapper.sh'
    assert rv32_simple['miDebuggerServerAddress'] == '127.0.0.1:${input:rv32qemuGdbPort}'
    assert rv32_simple['preLaunchTask'] == 'rv32qemu: start simple test gdbstub'
    assert rv32_simple['postDebugTask'] == 'rv32qemu: stop debug session'
    assert rv32_simple['stopAtConnect'] is True
    assert rv32_simple['launchCompleteCommand'] == 'None'
    assert 'sourceFileMap' not in rv32_simple
    assert not any('substitute-path' in item['text'] for item in rv32_simple['setupCommands'])
    assert any(item['text'] == 'tbreak main' for item in rv32_simple['setupCommands'])

    backend = configs['Audio Studio GUI: Backend Debug Server']
    assert backend['program'].endswith('/out/linux/simulator/gui_backend/Debug/audio_studio_gui_server')
    backend_args = backend['args']
    assert backend_args[:2] == ['${workspaceFolder}/Audio-Studio', '${input:audioStudioServerPort}']
    for option in [
        '--as-server',
        '--alsatplg',
        '--as-server-rpc-mode',
        '--as-server-host',
        '--as-server-port',
        '--validation-python',
        '--validation-script',
        '--validation-as-log',
        '--validation-trace-ldc',
        '--validation-ready-timeout-ms',
        '--validation-datalink',
        '--validation-qemu-gdb-port',
        '--validation-qemu-gdb-wait',
        '--runtime-as-server-host',
        '--runtime-as-server-port',
        '--audio-driver-factory',
    ]:
        assert option in backend_args, f'missing backend arg {option}'
    for option in [
        '--validation-as-server',
        '--validation-as-server-host',
        '--validation-as-server-port',
        '--validation-use-existing-as-server',
    ]:
        assert option not in backend_args, f'legacy validation as_server arg must be removed: {option}'
    assert backend_args[backend_args.index('--as-server-rpc-mode') + 1] == 'socket'
    assert backend_args[backend_args.index('--as-server-port') + 1] == '${input:audioStudioAsServerPort}'
    assert backend_args[backend_args.index('--runtime-as-server-port') + 1] == '${input:audioStudioAsServerPort}'
    assert backend_args[backend_args.index('--validation-qemu-gdb-wait') + 1] == 'true'
    assert backend.get('environment', []) == []

    backend_source = ''
    for path in [
        ROOT / 'GUI' / 'backend' / 'include' / 'audio_studio.hpp',
        ROOT / 'GUI' / 'backend' / 'src' / 'main.cpp',
        ROOT / 'GUI' / 'backend' / 'src' / 'project_orchestration.cpp',
    ]:
        backend_source += path.read_text(encoding='utf-8')
    for forbidden in [
        'std::getenv',
        'getenv(',
        'AUDIO_STUDIO_AS_SERVER_',
        'AUDIO_STUDIO_VALIDATION_',
        'AUDIO_STUDIO_GUI_AUDIO_DRIVER_FACTORY',
        'application/rv32qemu',
        'rv32qemu',
        'out/linux/simulator',
    ]:
        require_not_contains(backend_source, forbidden)

    tasks = json.loads((VASS_ROOT / '.vscode' / 'tasks.json').read_text(encoding='utf-8'))
    tasks_text = (VASS_ROOT / '.vscode' / 'tasks.json').read_text(encoding='utf-8')
    require_not_contains(tasks_text, 'audio-studio-gui-debug-build.pid')
    require_not_contains(tasks_text, 'build-response.json')
    task_inputs = {item['id']: item for item in tasks.get('inputs', [])}
    task_input_refs = set(re.findall(r'\$\{input:([^}]+)\}', tasks_text))
    assert task_input_refs <= task_inputs.keys(), (
        f'missing tasks.json inputs: {sorted(task_input_refs - task_inputs.keys())}'
    )
    assert task_inputs['rv32qemuGdbPort']['default'] == '1235'
    task_by_label = {item['label']: item for item in tasks['tasks']}
    start_task = task_by_label['Audio Studio GUI: wait simulator qemu gdbstub']
    assert start_task['command'] == '${workspaceFolder}/.vscode/audio-studio-start-qemu-gdbstub.sh'
    assert start_task['args'] == [
        '${input:audioStudioQemuGdbPort}', '${workspaceFolder}',
        '${input:audioStudioServerPort}', '${input:audioStudioAsServerPort}'
    ]
    build_backend = task_by_label['Audio Studio: build gui backend debug']
    assert build_backend['args'][-1] == 'simulator'

    rv32_start_task = task_by_label['rv32qemu: start simple test gdbstub']
    assert rv32_start_task['command'] == '${workspaceFolder}/.vscode/rv32qemu-start-gdbstub.sh'
    assert rv32_start_task['args'] == [
        '${input:rv32qemuGdbPort}', '${workspaceFolder}',
        '${workspaceFolder}/Misc/sof_test/simple_test/splay-test-lists.txt'
    ]
    rv32_stop_task = task_by_label['rv32qemu: stop debug session']
    assert rv32_stop_task['command'] == '${workspaceFolder}/.vscode/rv32qemu-stop-debug.sh'
    assert rv32_stop_task['args'] == ['${input:rv32qemuGdbPort}']

    starter = (VASS_ROOT / '.vscode' / 'audio-studio-start-qemu-gdbstub.sh').read_text(encoding='utf-8')
    require_not_contains(starter, 'audio-studio-start-gui-debug-validation.py')
    require_not_contains(starter, 'setsid python3')
    require_not_contains(starter, '/api/pipeline/build')
    require_not_contains(starter, 'kill "$pid"')
    require_not_contains(starter, 'rm -f "$helper_pid_file" "$qemu_pid_file"')
    require_not_contains(starter, '</dev/tcp/127.0.0.1/"$port"')
    require_not_contains(starter, '</dev/tcp/127.0.0.1/"$as_server_port"')
    require_contains(starter, 'is_tcp_listening')
    require_contains(starter, '/proc/net/tcp')
    require_contains(starter, 'backend_api_ready')
    require_contains(starter, '--connect-timeout 0.2 --max-time 0.5')
    require_contains(starter, 'if ! is_tcp_listening "$backend_port"; then')
    assert (
        starter.index('if is_tcp_listening "$port"; then')
        < starter.index('if backend_api_ready; then')
    )
    require_contains(starter, 'backend_port=')
    require_contains(starter, 'as_server_port=')
    require_contains(starter, 'audio-studio-gui-debug-helper.pid')
    require_contains(starter, 'audio-studio-gui-debug-qemu.pid')
    require_contains(starter, 'Waiting for frontend Build')
    assert not (VASS_ROOT / '.vscode' / 'audio-studio-start-gui-debug-validation.py').exists()

    stopper = (VASS_ROOT / '.vscode' / 'audio-studio-stop-gui-debug.sh').read_text(encoding='utf-8')
    require_not_contains(stopper, 'audio-studio-gui-debug-build.pid')

    rv32_starter = (VASS_ROOT / '.vscode' / 'rv32qemu-start-gdbstub.sh').read_text(encoding='utf-8')
    require_contains(rv32_starter, 'rv32qemu-debug')
    require_contains(rv32_starter, 'stop_debug_processes')
    require_contains(rv32_starter, '(pgrep -f "$pattern" || true)')
    require_contains(rv32_starter, 'sof-build-test.py')
    require_contains(rv32_starter, '--qemu-gdb-port')
    require_contains(rv32_starter, '--qemu-gdb-wait')
    require_contains(rv32_starter, '--gui-keep-alive')
    require_contains(rv32_starter, '--gui-ready-marker')
    require_contains(rv32_starter, '/proc/net/tcp')
    require_not_contains(rv32_starter, '/api/pipeline/build')
    require_not_contains(rv32_starter, 'audio-studio-gui-debug')

    rv32_stopper = (VASS_ROOT / '.vscode' / 'rv32qemu-stop-debug.sh').read_text(encoding='utf-8')
    require_contains(rv32_stopper, 'stop_debug_processes')
    require_contains(rv32_stopper, '(pgrep -f "$pattern" || true)')
    require_contains(rv32_stopper, 'rv32qemu-debug-helper.pid')
    require_contains(rv32_stopper, 'rv32qemu-debug-qemu.pid')
    require_contains(rv32_stopper, 'qemu-system-riscv32.*-gdb tcp::')
    require_contains(rv32_stopper, '--qemu-gdb-port')
    require_not_contains(rv32_stopper, 'audio-studio-gui-debug')

    gdb_wrapper = (VASS_ROOT / '.vscode' / 'riscv-gdb-wrapper.sh').read_text(encoding='utf-8')
    require_contains(gdb_wrapper, 'riscv64-unknown-elf-gdb')
    require_contains(gdb_wrapper, 'libncursesw.so.5')

    inputs = {item['id']: item for item in launch['inputs']}
    assert inputs['rv32qemuGdbPort']['default'] == '1235'

    compounds = {item['name']: item for item in launch['compounds']}
    full_stack = compounds['Audio Studio GUI: Full Stack Debug']
    assert full_stack['configurations'] == [
        'Audio Studio GUI: Backend Debug Server',
        'Audio Studio GUI: Frontend Debug Chrome (Mac)',
    ]


if __name__ == '__main__':
    main()
