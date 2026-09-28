#!/usr/bin/env python3
"""Retain important events across native process restarts and read them over CSP."""

import argparse
import json
from pathlib import Path
import re
import struct
import time
import serial
from firmware_fixture import NativeNode, BenchNode, packets, send_packet


def settled(node, minimum=1):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        row = node.command('cmd journal_stats', r'journal_stats node=0: OK ([^\r\n]+)')[1]
        values = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', row)}
        if values['ready'] == 1 and values['queued'] == 0 and values['held'] >= minimum:
            return values
        time.sleep(.1)
    raise AssertionError(f'journal did not settle: {row}')


def read_record(node, age=0):
    return node.command(f'cmd journal_tail {age}', r'journal_tail node=0: OK ([^\r\n]+)')[1]


def remote_record(device, target, age, baud):
    body = struct.pack('>BBBBHHHH', 1, 1, 0, 1, 10, 7, 7, 0) + bytes([1, 0, 4]) + age.to_bytes(4, 'big')
    with serial.Serial(device, baud, timeout=.02, write_timeout=1) as port:
        send_packet(port, 30, target, 48, 11, body)
        for header, reply in packets(port):
            if header['source'] == target and header['destination'] == 30 and header['sport'] == 11:
                assert reply[:4] == bytes([1, 2, 0, 0]), reply.hex()
                assert int.from_bytes(reply[4:6], 'big') == 10
                assert int.from_bytes(reply[6:8], 'big') == 7
                return reply[12:].decode()
    raise AssertionError('missing journal reply')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--executable')
    mode.add_argument('--serial')
    parser.add_argument('--kiss-device')
    parser.add_argument('--node', type=int, default=1)
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--expected-previous-boot', type=int)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.serial and not args.kiss_device:
        parser.error('--serial requires --kiss-device')
    if args.node == 30 or not 0 < args.node < 16383:
        parser.error('target must be unicast and differ from source 30')
    args.output.mkdir(parents=True, exist_ok=False)
    flash = args.output / 'flash.bin'
    fixture = NativeNode(args.executable, args.output / 'first', flash) if args.executable else BenchNode(args.serial, args.kiss_device, args.output / 'first')
    with fixture as node:
        settled(node)
        node.command('cmd journal_missing_command', r'unknown command')
        settled(node, 2)
        first = read_record(node)
        assert 'src=2 id=2 sev=1' in first, first
        timing = node.command('cmd journal_time 0', r'journal_time node=0: OK ([^\r\n]+)')[1]
        assert remote_record(node.device, args.node, 0, args.baud) == first
        if args.expected_previous_boot is not None:
            count = settled(node)['held']
            assert any(f'boot={args.expected_previous_boot} ' in read_record(node, age)
                       for age in range(count)), 'previous boot absent'
    result = {'first': first, 'time': timing, 'remote_read': 'passed'}
    if args.executable:
        with NativeNode(args.executable, args.output / 'second', flash) as node:
            settled(node, 3)
            boot = read_record(node)
            assert 'boot=2 src=1 id=1' in boot, boot
            assert read_record(node, 1) == first
            assert remote_record(node.device, 1, 1, args.baud) == first
            assert node.command('cmd journal_time 1', r'journal_time node=0: OK ([^\r\n]+)')[1] == timing
            result['restart'] = 'passed'
            result['second_boot'] = boot
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('JOURNAL SMOKE RESULT: PASS')


if __name__ == '__main__':
    main()
