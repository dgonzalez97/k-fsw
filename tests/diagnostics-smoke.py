#!/usr/bin/env python3
"""Exercise HK capture/replay and CSP diagnostics on native_sim or an explicit bench."""

import argparse
import contextlib
import importlib.util
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[1]
BRIDGE = REPO / 'tools/ground/hk-bridge.py'
spec = importlib.util.spec_from_file_location('hk_bridge', BRIDGE)
hk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hk)


def run(command, **kwargs):
    result = subprocess.run(command, text=True, capture_output=True, timeout=30, **kwargs)
    if result.returncode:
        raise RuntimeError(f'{command}: {result.stdout}\n{result.stderr}')
    return result.stdout


def wait_for(read, pattern, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = read()
        if match := re.search(pattern, text):
            return match
        time.sleep(.05)
    raise RuntimeError(f'missing {pattern}:\n{read()}')


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--executable', type=Path)
    mode.add_argument('--serial', help='physical shell UART; never discovered automatically')
    parser.add_argument('--kiss-device', help='physical CSP UART, required with --serial')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--node', type=int, default=1)
    parser.add_argument('--csp-tool', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='new evidence directory')
    args = parser.parse_args()
    if args.serial and not args.kiss_device:
        parser.error('--serial requires --kiss-device')
    args.output.mkdir(parents=True, exist_ok=False)
    with contextlib.ExitStack() as stack:
        if args.executable:
            log = args.output / 'node.log'
            stream = stack.enter_context(log.open('w'))
            process = subprocess.Popen([str(args.executable.resolve()), '--uart_stdinout', '--no-color',
                                     f'-flash={args.output.resolve() / "flash.bin"}'],
                                    stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT,
                                    text=True)
            stack.callback(stop, process)
            stack.callback(process.stdin.close)
            read_log = log.read_text

            def send(*commands):
                process.stdin.write('\n'.join(commands) + '\n')
                process.stdin.flush()

            wait_for(read_log, r'@READY ')
            device = wait_for(read_log, r'uart_1 connected to pseudotty: (\S+)')[1]
        else:
            import serial
            shell = stack.enter_context(serial.Serial(args.serial, 115200, timeout=.1))
            log = args.output / 'node.log'
            captured = ''

            def read_log():
                nonlocal captured
                captured += shell.read(shell.in_waiting or 1).decode(errors='replace')
                log.write_text(captured)
                return captured

            def send(*commands):
                shell.write(('\n'.join(commands) + '\n').encode())
                shell.flush()

            device = args.kiss_device
        node = str(args.node)
        bridge = [sys.executable, str(BRIDGE), '--device', device, '--baud', str(args.baud),
                  '--node', node, '--timeout', '2', '--once']
        send('hk define 0 1:0 3:0', 'hk define 1 1:0 3:0', 'hk collect 0', 'hk collect 1',
             'hk get 1')
        wait_for(read_log, r'report 1')
        capture = args.output / 'poll.jsonl'
        output = run(bridge + ['--capture', str(capture), '--yamcs', 'none'])
        (args.output / 'poll.log').write_text(output)
        assert 'report 0 ' in output, output
        records = list(hk.read_capture(stack.enter_context(capture.open())))
        assert records, 'empty polling capture'
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sink:
            sink.bind(('127.0.0.1', 0))
            sink.settimeout(2)
            run([sys.executable, str(BRIDGE), '--replay', str(capture), '--yamcs',
                 f'127.0.0.1:{sink.getsockname()[1]}'])
            for _, sample, received in records:
                assert sink.recv(1024) == hk.envelope(sample, received)
        send('hk period 0 1000', 'hk period 1 1000', 'hk beacon 0 16 5000', 'hk beacon 1 16 5000')
        beacons = args.output / 'beacons.jsonl'
        output = run(bridge + ['--listen', '--timeout', '7', '--capture', str(beacons), '--yamcs', 'none'])
        (args.output / 'beacons.log').write_text(output)
        reports = {body[1] for _, body, _ in hk.read_capture(stack.enter_context(beacons.open()))}
        assert reports == {0, 1}, reports
        send('hk beacon 0 16 0', 'hk beacon 1 16 0', 'hk period 0 0', 'hk period 1 0',
             f'csp ifstat {node} KISS')
        wait_for(read_log, rf'CSP ifstat node={node} interface=KISS tx=\d+ rx=\d+')
        bad = subprocess.run(bridge + ['--report', '15', '--timeout', '.2', '--yamcs', 'none'],
                             text=True, capture_output=True, timeout=5)
        assert bad.returncode != 0 and 'requested samples' in bad.stderr, bad
        if args.csp_tool:
            tool = [str(args.csp_tool.resolve()), '--device', device, '--baud', str(args.baud)]
            output = run(tool + ['ping', '--node', node, '--count', '3'])
            assert 'PING RESULT: PASS received=3' in output, output
            (args.output / 'ping.log').write_text(output)
            output = run(tool + ['ifstat', '--node', node, '--interface', 'KISS'])
            assert 'interface=KISS' in output and 'rxbytes=' in output, output
            (args.output / 'ifstat.log').write_text(output)
            bad = subprocess.run(tool + ['--timeout-ms', '100', 'ifstat', '--node', node,
                                         '--interface', 'missing'], text=True, capture_output=True, timeout=5)
            assert bad.returncode and 'timeout' in bad.stderr, bad
            pcap = args.output / 'traffic.pcap'
            dump = subprocess.Popen(tool + ['dump', '--pcap-file', str(pcap), '--seconds', '2'],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                wait_for(lambda: str(pcap.exists()), 'True')
                send('csp ping 16')
                stdout, stderr = dump.communicate(timeout=5)
                assert dump.returncode == 0, stderr
                (args.output / 'capture.log').write_text(stdout)
            finally:
                stop(dump)
            data = pcap.read_bytes()
            endian = '<' if data[:4] == b'\xd4\xc3\xb2\xa1' else '>'
            assert struct.unpack_from(endian + 'I', data, 20)[0] == 147, 'PCAP must be USER0'
            offset = 24
            packets = 0
            while offset < len(data):
                _seconds, _fraction, length, original = struct.unpack_from(endian + 'IIII', data, offset)
                packet = data[offset + 16:offset + 16 + length]
                assert len(packet) == length == original and length >= 10
                assert hk.parse_csp_header(packet)['source'] == args.node
                assert packet[-4:] == hk.crc32(packet[6:-4])
                offset += 16 + length
                packets += 1
            assert packets and offset == len(data)
        print(f'DIAGNOSTICS SMOKE RESULT: PASS ({args.output})')


if __name__ == '__main__':
    main()
