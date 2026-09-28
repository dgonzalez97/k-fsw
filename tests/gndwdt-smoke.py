#!/usr/bin/env python3
"""Check ground watchdog feeds over CSP against the Linux composition."""

import argparse
import struct
import time

import serial

from firmware_fixture import NativeNode, packets, send_packet

WORD = b'KFSWWSFK'
COMMAND_ID = 16


def request(word=WORD, command=COMMAND_ID, version=1, opcode=1, token=0):
    payload = struct.pack('>BH', 3, len(word)) + word
    header = struct.pack('>BBBBHHHH', version, opcode, 0, 1, command, 7, len(payload), 0)
    return header + (struct.pack('>Q', token) if version == 2 else b'') + payload


def exchange(port, body, source=16, dport=11):
    send_packet(port, source, 1, 48, dport, body)
    for header, reply in packets(port):
        if (header['destination'] == source and header['source'] == 1
                and header['sport'] == dport and header['dport'] == 48):
            return reply
    raise AssertionError('missing CSP reply')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    with NativeNode(args.executable, args.output) as node:
        assert node.parameter('gndwdt_timeout_s') == 432000
        node.command('gndwdt timeout 431999', 'Timeout refused:')
        node.command('cmd ground_wtd KFSWWSFK', 'ground_wtd node=0: denied')
        node.command('gndwdt contact', 'Subcommands:')
        with serial.Serial(node.device, 115200, timeout=.02, write_timeout=1) as port:
            count = node.parameter('gndwdt_contacts')
            assert count == 0
            assert exchange(port, WORD, dport=1) == WORD  # Ping must not feed.
            assert exchange(port, request(command=999))[2] == 1
            for word, status in [(b'', 3), (b'kfswwsfk', 3), (b'KFSWWSF', 3),
                                 (WORD + b'x', 3), (WORD + b'\0x', 2)]:
                assert exchange(port, request(word))[2] == status
            assert node.parameter('gndwdt_contacts') == count
            for source in (2, 16):
                assert exchange(port, request(), source=source)[2] == 0
                count += 1
                assert node.parameter('gndwdt_contacts') == count
                assert node.parameter('gndwdt_last_node') == source
            time.sleep(2)
            since = node.parameter('gndwdt_since_s')
            assert since >= 2
            node.command('gndwdt timeout 432000', 'Ground watchdog timeout_s: 432000')
            node.command('gndwdt on', 'Ground watchdog on')
            assert node.parameter('gndwdt_since_s') >= since

            # Retrying the same ticket returns its result without feeding twice.
            prepared = exchange(port, request(version=2, opcode=3, token=1234))
            assert prepared[2] == 0
            ticket = int.from_bytes(prepared[12:20], 'big')
            assert ticket != 0
            assert node.parameter('gndwdt_contacts') == count
            execute = request(version=2, opcode=5, token=ticket)
            reply = exchange(port, execute)
            assert reply[2] == 0
            assert exchange(port, execute) == reply
            assert node.parameter('gndwdt_contacts') == count + 1
    print('GNDWDT RESULT: PASS')


if __name__ == '__main__':
    main()
