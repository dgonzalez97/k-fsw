#!/usr/bin/env python3
"""Exercise ticketed commands against native firmware and a fault-injecting peer."""

import argparse
import json
import struct
import serial
from firmware_fixture import NativeNode, BenchNode, packets, send_packet

REBOOT = 3


def message(opcode, token=0, command=1, request=7, version=2, status=0):
    header = struct.pack('>BBBBHHHH', version, opcode, status, 0, command, request, 0, 0)
    return header + (struct.pack('>Q', token) if version == 2 else b'')


def exchange(port, body, source=30, node=1):
    send_packet(port, source, node, 48, 11, body)
    for header, reply in packets(port):
        if header['destination'] == source and header['source'] == node and header['sport'] == 11:
            return reply
    raise AssertionError('missing command reply')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--executable')
    mode.add_argument('--serial')
    parser.add_argument('--kiss-device')
    parser.add_argument('--node', type=int, default=1)
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if args.serial and not args.kiss_device:
        parser.error('--serial requires --kiss-device')
    if args.node in [30, 31] or not 0 < args.node < 16383:
        parser.error('target must be unicast and differ from test sources 30 and 31')
    fixture = NativeNode(args.executable, args.output) if args.executable else BenchNode(args.serial, args.kiss_device, args.output)
    with fixture as node:
        with serial.Serial(node.device, args.baud, timeout=.02, write_timeout=1) as port:
            def call(body, source=30):
                return exchange(port, body, source=source, node=args.node)

            before = node.parameter('cmd_invoked')
            prepared = call(message(3, 1234))
            assert prepared[:4] == bytes([2, 4, 0, 0]), prepared.hex()
            token = int.from_bytes(prepared[12:20], 'big')
            assert token
            assert call(message(3, 1234)) == prepared
            executed = call(message(5, token))
            # Treat this first result as lost, then retry the same request.
            assert call(message(5, token)) == executed
            assert executed[2] == 0, executed.hex()
            assert node.parameter('cmd_invoked') == before + 1
            assert call(message(5, token, command=2))[2] == 2
            assert call(message(5, token), source=31)[2] == 6
            assert node.parameter('cmd_invoked') == before + 1
            legacy = call(message(1, version=1))
            assert legacy[0] == 1 and legacy[2] == 0
            assert node.parameter('cmd_invoked') == before + 2

            if not args.executable:
                (node.output / 'result.json').write_text(json.dumps({'server_handler_delta': 1, 'legacy_call': 'passed'}) + '\n')
                print('COMMAND RETRY SMOKE RESULT: PASS')
                return

            node.send('param set cmd_timeout_ms 1000')
            node.command('param get cmd_timeout_ms', r'cmd_timeout_ms = 1000')
            # A reboot is the kind of request a retry must not run twice.
            offset = node.send('csp reboot 2 0000 --retry')
            prepared_requests = []
            executed_requests = []
            peer_invocations = 0
            for header, data in packets(port, 8):
                if header['destination'] != 2 or header['dport'] != 11:
                    continue
                assert data[0] == 2, data.hex()
                opcode = data[1]
                if opcode == 3:
                    prepared_requests.append(data)
                    if len(prepared_requests) == 1:
                        continue
                    reply = message(4, 9876, command=REBOOT, request=int.from_bytes(data[6:8], 'big'))
                else:
                    assert opcode == 5 and int.from_bytes(data[12:20], 'big') == 9876
                    executed_requests.append(data)
                    if len(executed_requests) == 1:
                        peer_invocations += 1
                        continue
                    reply = message(2, 9876, command=REBOOT, request=int.from_bytes(data[6:8], 'big'))
                send_packet(port, 2, header['source'], 11, header['sport'], reply)
                if len(executed_requests) == 2:
                    break
            node.wait(r'node: 2', offset)
            assert len(prepared_requests) == 2 and prepared_requests[0] == prepared_requests[1]
            assert len(executed_requests) == 2 and executed_requests[0] == executed_requests[1]
            assert peer_invocations == 1

            offset = node.send('csp reboot 2 0000 --retry')
            requests = []
            for header, data in packets(port, 3):
                if header['destination'] == 2 and header['dport'] == 11:
                    requests.append(data)
                    send_packet(port, 2, header['source'], 11, header['sport'],
                                message(2, version=1, command=0, request=0, status=2))
            node.wait(r'Node 2 did not answer', offset)
            assert len(requests) == 1 and requests[0][0:2] == bytes([2, 3])
        result = {'server_handler_delta': 1, 'client_prepare_attempts': 2,
                  'client_execute_attempts': 2, 'client_peer_invocations': peer_invocations,
                  'legacy_call': 'passed', 'old_peer_fallback': False}
        (node.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print('COMMAND RETRY SMOKE RESULT: PASS')


if __name__ == '__main__':
    main()
