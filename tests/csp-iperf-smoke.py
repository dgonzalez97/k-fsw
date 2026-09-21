#!/usr/bin/env python3
"""CSP 2 benchmark on native_sim or an explicitly selected bench KISS device."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--executable', type=Path)
    target.add_argument('--device')
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--node', default='1')
    parser.add_argument('--source', default='30')
    parser.add_argument('--baud', default='115200')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    node = None
    node_log = None
    try:
        device = args.device
        if args.executable:
            log_path = args.output / 'node.log'
            node_log = log_path.open('w')
            node = subprocess.Popen([str(args.executable.resolve()), '--uart_stdinout',
                                     '--no-color', f'-flash={args.output.resolve() / "flash.bin"}'],
                                    stdin=subprocess.PIPE, stdout=node_log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                text = log_path.read_text()
                match = re.search(r'uart_1 connected to pseudotty: (\S+)', text)
                if match and '@READY ' in text:
                    device = match[1]
                    break
                if node.poll() is not None:
                    raise RuntimeError(f'native node exited: {text}')
                time.sleep(.05)
            if not device:
                raise RuntimeError('native node did not publish its KISS PTY and readiness')
        command = [str(args.tool.resolve()), '--device', device, '--baud', args.baud,
                   '--src-addr', args.source, '--dest-addr', args.node, '--packet-size', '64',
                   '--tx-rate', '640', '--duration', '2', '--reply-timeout', '1', '--json']
        (args.output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
        result = subprocess.run(command, text=True, capture_output=True, timeout=10)
        (args.output / 'result.json').write_text(result.stdout)
        (args.output / 'stderr.log').write_text(result.stderr)
        if result.returncode:
            raise RuntimeError(f'benchmark failed ({result.returncode}): {result.stderr}\n{result.stdout}')
        row = json.loads(result.stdout)
        assert row['transport'] == 'csp2-kiss', row
        assert row['sent'] >= 10 and row['received'] == row['sent'] and row['lost'] == 0, row
        assert row['rtt_min_ms'] is not None and row['rtt_max_ms'] < 1000, row
        print(json.dumps(row, indent=2))
        print('CSP IPERF SMOKE RESULT: PASS')
    finally:
        if node:
            if node.poll() is None:
                node.terminate()
                try:
                    node.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    node.kill()
                    node.wait()
            node.stdin.close()
        if node_log:
            node_log.close()


if __name__ == '__main__':
    main()
