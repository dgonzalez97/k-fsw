#!/usr/bin/env python3
"""Check UTC procedure waits using explicit clock set/get on a dedicated node."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from firmware_fixture import NativeNode, BenchNode, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--executable')
    mode.add_argument('--serial', help='dedicated bench console; utc-wait.txt must be uploaded first')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    flash = args.output / 'flash.bin'
    if args.executable:
        subprocess.run([sys.executable, str(ROOT / 'tools/ground/stage-file.py'),
                        '--flash', str(flash), '--offset', '0xfc000', '--size', '0x40000',
                        str(ROOT / 'tests/procedures/utc-wait.txt'), '/ftp/procedures/utc-wait.txt'],
                       check=True, capture_output=True, text=True, timeout=10)
    fixture = NativeNode(args.executable, args.output / 'node', flash) if args.executable else BenchNode(args.serial, None, args.output / 'node')
    with fixture as node:
        original = node.command('csp clock', r'clock: ([^\r\n]+)')[1]
        saved = re.search(r'UTC \((\d+)\.', original)
        start = time.monotonic()
        try:
            before = node.parameter('cmd_invoked')
            node.command('csp clock set 1900000000', r'clock: .*UTC')
            offset = node.send('fbo run utc-wait.txt')
            node.wait(r'FBO: utc-wait.txt finished at line 2 \(0\)', offset)
            assert node.parameter('cmd_invoked') == before + 1
            node.command('fbo status', r'last result: 0')

            node.command('csp clock set 1900000010', r'clock: .*UTC')
            offset = node.send('fbo run utc-wait.txt')
            node.wait(r'FBO: utc-wait.txt line 1 failed', offset)
            node.command('fbo status', r'running: no')
            assert node.parameter('cmd_invoked') == before + 1

            node.command('csp clock set 1899999990', r'clock: .*UTC')
            node.command('fbo run utc-wait.txt', r'utc-wait.txt started')
            time.sleep(.1)
            offset = node.send('fbo stop')
            node.wait(r'FBO: utc-wait.txt finished at line 1 \(-\d+\)', offset, timeout=2)
            node.command('fbo status', r'running: no')
            assert node.parameter('cmd_invoked') == before + 1

            node.command('csp clock set 1', r'clock: not set')
            offset = node.send('fbo run utc-wait.txt')
            node.wait(r'FBO: utc-wait.txt line 1 failed', offset)
            node.command('fbo status', r'running: no')
            assert node.parameter('cmd_invoked') == before + 1
            (args.output / 'result.json').write_text(json.dumps({
                'due': 'passed', 'late': 'rejected', 'cancel': 'passed',
                'unset': 'rejected', 'handler_delta': 1}, indent=2) + '\n')
        finally:
            restored = int(saved[1]) + int(time.monotonic() - start) if saved else 1
            node.command(f'csp clock set {restored}', r'clock: ')
        print('FBO UTC SMOKE RESULT: PASS')


if __name__ == '__main__':
    main()
