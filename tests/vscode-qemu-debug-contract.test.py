#!/usr/bin/env python3
import json
import importlib.util
import pathlib


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
    require_contains(helper, 'parser.add_argument("--use-existing-as-server"')
    require_contains(helper, '-gdb tcp::')

    launch = json.loads((VASS_ROOT / '.vscode' / 'launch.json').read_text(encoding='utf-8'))
    configs = {item['name']: item for item in launch['configurations']}
    sim = configs['Audio Studio GUI: Simulator Keep Alive']
    assert sim['type'] == 'cppdbg'
    assert sim['request'] == 'launch'
    assert sim['program'].endswith('/application/rv32qemu/build/sof_freertos_riscv')
    assert sim['miDebuggerPath'] == '${workspaceFolder}/.vscode/riscv-gdb-wrapper.sh'
    assert sim['miDebuggerServerAddress'] == '127.0.0.1:${input:audioStudioQemuGdbPort}'
    assert sim['preLaunchTask'] == 'Audio Studio GUI: start simulator qemu gdbstub'
    assert sim['launchCompleteCommand'] == 'exec-continue'
    assert any(item['text'] == 'break main' and not item['ignoreFailures'] for item in sim['setupCommands'])
    assert 'debugServerPath' not in sim
    assert 'debugServerArgs' not in sim
    assert 'serverStarted' not in sim
    assert 'filterStdout' not in sim
    assert 'filterStderr' not in sim

    backend = configs['Audio Studio GUI: Backend Debug Server']
    assert backend['program'].endswith('/out/linux/simulator/gui_backend/Debug/audio_studio_server')
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
        '--validation-as-server',
        '--validation-as-log',
        '--validation-trace-ldc',
        '--validation-as-server-host',
        '--validation-as-server-port',
        '--validation-ready-timeout-ms',
        '--validation-use-existing-as-server',
        '--validation-datalink',
        '--validation-qemu-gdb-port',
        '--validation-qemu-gdb-wait',
        '--runtime-as-server-host',
        '--runtime-as-server-port',
        '--audio-driver-factory',
    ]:
        assert option in backend_args, f'missing backend arg {option}'
    assert backend_args[backend_args.index('--as-server-rpc-mode') + 1] == 'socket'
    assert backend_args[backend_args.index('--as-server-port') + 1] == '${input:audioStudioAsServerPort}'
    assert backend_args[backend_args.index('--validation-as-server-port') + 1] == '${input:audioStudioAsServerPort}'
    assert backend_args[backend_args.index('--runtime-as-server-port') + 1] == '${input:audioStudioAsServerPort}'
    assert backend_args[backend_args.index('--validation-use-existing-as-server') + 1] == 'true'
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
    task_by_label = {item['label']: item for item in tasks['tasks']}
    start_task = task_by_label['Audio Studio GUI: start simulator qemu gdbstub']
    assert start_task['command'] == '${workspaceFolder}/.vscode/audio-studio-start-qemu-gdbstub.sh'
    assert start_task['args'] == [
        '${input:audioStudioQemuGdbPort}', '${workspaceFolder}',
        '${input:audioStudioServerPort}', '${input:audioStudioAsServerPort}'
    ]
    build_backend = task_by_label['Audio Studio: build gui backend debug']
    assert build_backend['args'][-1] == 'simulator'

    starter = (VASS_ROOT / '.vscode' / 'audio-studio-start-qemu-gdbstub.sh').read_text(encoding='utf-8')
    require_contains(starter, 'audio-studio-start-gui-debug-validation.py')
    require_contains(starter, 'setsid python3')
    require_contains(starter, 'backend_port=')
    require_contains(starter, 'as_server_port=')
    require_contains(starter, '/dev/tcp/127.0.0.1/"$port"')
    require_contains(starter, 'audio-studio-gui-debug-build.pid')

    build_launcher = (VASS_ROOT / '.vscode' / 'audio-studio-start-gui-debug-validation.py').read_text(encoding='utf-8')
    require_contains(build_launcher, '/api/pipeline/build')
    require_contains(build_launcher, '"build_scope": "all_pipelines"')
    require_contains(build_launcher, 'frontend_connections')

    spec = importlib.util.spec_from_file_location(
        'audio_studio_gui_debug_validation',
        VASS_ROOT / '.vscode' / 'audio-studio-start-gui-debug-validation.py',
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    simulator_config = json.loads((ROOT / 'configs/platform/simulator/simulator.json').read_text())
    simulator_config['workspace_id'] = 'debug-contract'
    snapshot = module.graph_snapshot(simulator_config)
    assert len(snapshot['working_groups']) == len(simulator_config['pipelines'])

    gdb_wrapper = (VASS_ROOT / '.vscode' / 'riscv-gdb-wrapper.sh').read_text(encoding='utf-8')
    require_contains(gdb_wrapper, 'riscv64-unknown-elf-gdb')
    require_contains(gdb_wrapper, 'libncursesw.so.5')

    compounds = {item['name']: item for item in launch['compounds']}
    full_stack = compounds['Audio Studio GUI: Full Stack Debug']
    assert full_stack['configurations'] == [
        'Audio Studio GUI: Simulator Keep Alive',
        'Audio Studio GUI: as_server Debug',
        'Audio Studio GUI: Backend Debug Server',
        'Audio Studio GUI: Frontend Debug Chrome (Mac)',
    ]


if __name__ == '__main__':
    main()
