#!/usr/bin/env python3
import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
VASS_ROOT = ROOT.parent


def require_contains(text, expected):
    assert expected in text, f'missing {expected!r}'


def main():
    helper = (VASS_ROOT / 'application' / 'rv32qemu' / 'sof-build-test.py').read_text(encoding='utf-8')
    require_contains(helper, 'parser.add_argument("--qemu-gdb-port"')
    require_contains(helper, 'parser.add_argument("--qemu-gdb-wait"')
    require_contains(helper, '-gdb tcp::')

    launch = json.loads((VASS_ROOT / '.vscode' / 'launch.json').read_text(encoding='utf-8'))
    configs = {item['name']: item for item in launch['configurations']}
    sim = configs['Audio Studio GUI: Simulator Keep Alive']
    assert sim['type'] == 'cppdbg'
    assert sim['request'] == 'launch'
    assert sim['program'].endswith('/application/rv32qemu/build/sof_freertos_riscv')
    assert sim['miDebuggerServerAddress'] == '127.0.0.1:${input:audioStudioQemuGdbPort}'
    require_contains(sim['debugServerArgs'], '--qemu-gdb-port ${input:audioStudioQemuGdbPort}')
    require_contains(sim['debugServerArgs'], '--qemu-gdb-wait')

    compounds = {item['name']: item for item in launch['compounds']}
    full_stack = compounds['Audio Studio GUI: Full Stack Debug']
    assert 'Audio Studio GUI: Simulator Keep Alive' in full_stack['configurations']


if __name__ == '__main__':
    main()
