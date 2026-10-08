#!/usr/bin/env python3
"""Unprompted HK over native_sim PTY KISS; no UART wiring or RF evidence."""

import argparse
import os
from pathlib import Path
import struct
import tempfile
import time

import serial

from firmware_fixture import NativeNode, packets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', default=str(Path(os.environ.get(
        'KFSW_OUTPUT_ROOT', Path(__file__).resolve().parents[2] / 'build')) / 'linux/zephyr/zephyr.exe'))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='kfsw-hk-beacon-') as directory:
        with NativeNode(args.executable, Path(directory) / 'node') as node:
            node.command('hk define 0 24:8 24:12', 'report 0')
            node.command('hk period 0 1000', 'report 0 every 1000 ms')
            time.sleep(2)
            node.command('hk beacon 0 16 250', 'beacon interval must be 0 or at least 5000 ms')
            print('  [ok]   an interval under the floor is refused')
            with serial.Serial(node.device, 115200, timeout=.05) as port:
                node.command('hk beacon 0 16 5000', 'report 0 beacons')
                samples = []
                for header, data in packets(port, 12):
                    if header['source'] == 1 and header['destination'] == 16 and data[:2] == b'\x01\x00':
                        samples.append(data)
                assert len(samples) >= 2, f'only {len(samples)} beacons'
                sequences = []
                for data in samples:
                    version, report, sequence, seconds, entries, flags, unsigned, signed = struct.unpack('>BBHIBBIi', data)
                    assert (version, report, entries, flags, unsigned, signed) == (1, 0, 2, 0, 42, -7)
                    sequences.append(sequence)
                assert all(right > left for left, right in zip(sequences, sequences[1:])), sequences
                print("  [ok]   the beacon is header plus the report's values: 42 and -7")
                print('  [ok]   beacons keep coming with advancing sequences')
                match = node.command('hk show', r'beacons sent: (\d+)')
                assert int(match[1]) >= len(samples)
                print('  [ok]   the node counted what it sent')
                node.command('hk beacon 0 16 0', 'report 0 stops beaconing')
                # Discard only traffic already queued at the acknowledgement.
                port.reset_input_buffer()
                after = [data for header, data in packets(port, 11)
                         if header['source'] == 1 and data[:2] == b'\x01\x00']
                assert not after, f'{len(after)} beacons after disable'
                print('  [ok]   an operator can take it away: silence for two intervals')
    print('HK BEACON SMOKE RESULT: PASS')


if __name__ == '__main__':
    main()
