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


def fields(node, command, last):
    """Run a command that answers one 'key: value' per line, ending with last."""
    offset = node.send(command)
    node.wait(rf'\n{last}: [^\r\n]+', offset)
    text = node.log.read_text()[offset:]
    return dict(re.findall(r'^(\w+): ([^\r\n]+?)\r?$', text, re.MULTILINE))


def settled(node, minimum=1):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        values = {key: int(value) for key, value in fields(node, 'journal stats', 'last').items()}
        if values['ready'] == 1 and values['queued'] == 0 and values['held'] >= minimum:
            return values
        time.sleep(.1)
    raise AssertionError(f'journal did not settle: {values}')


def read_record(node, age=0):
    return fields(node, f'journal tail {age}', 'data')


def request(device, target, baud, command, request_id, payload=b''):
    count = 1 if payload else 0
    body = struct.pack('>BBBBHHHH', 1, 1, 0, count, command, request_id, len(payload), 0) + payload
    with serial.Serial(device, baud, timeout=.02, write_timeout=1) as port:
        send_packet(port, 30, target, 48, 11, body)
        for header, reply in packets(port):
            if header['source'] == target and header['destination'] == 30 and header['sport'] == 11:
                assert reply[:2] == bytes([1, 2]), reply.hex()
                assert int.from_bytes(reply[4:6], 'big') == command
                assert int.from_bytes(reply[6:8], 'big') == request_id
                return reply
    raise AssertionError('missing command reply')


def remote_record(device, target, age, baud):
    reply = request(device, target, baud, 10, 7, bytes([1, 0, 4]) + age.to_bytes(4, 'big'))
    assert reply[2] == 0, reply.hex()
    return dict(re.findall(r'(\w+)=(\S+)', reply[12:].decode()))


def unknown_command(device, target, baud):
    """A request for a command ID nobody registered; the node journals it."""
    reply = request(device, target, baud, 999, 8)
    assert reply[2] != 0, reply.hex()


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
        unknown_command(node.device, args.node, args.baud)
        settled(node, 2)
        first = read_record(node)
        assert (first['src'], first['id'], first['sev']) == ('command', '2', '1'), first
        timing = fields(node, 'journal time 0', 'valid')
        assert remote_record(node.device, args.node, 0, args.baud) == first
        if args.expected_previous_boot is not None:
            count = settled(node)['held']
            assert any(read_record(node, age)['boot'] == str(args.expected_previous_boot)
                       for age in range(count)), 'previous boot absent'
    result = {'first': first, 'time': timing, 'remote_read': 'passed'}
    if args.executable:
        with NativeNode(args.executable, args.output / 'second', flash) as node:
            settled(node, 3)
            boot = read_record(node)
            assert (boot['boot'], boot['src'], boot['id']) == ('2', 'boot', '1'), boot
            assert read_record(node, 1) == first
            assert remote_record(node.device, 1, 1, args.baud) == first
            assert fields(node, 'journal time 1', 'valid') == timing
            result['restart'] = 'passed'
            result['second_boot'] = boot
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('JOURNAL SMOKE RESULT: PASS')


if __name__ == '__main__':
    main()
