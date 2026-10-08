#!/usr/bin/env python3
"""Ground class-mask requests over hosted PTY KISS; software evidence only."""

import importlib.util
import os
from pathlib import Path
import struct
import tempfile

import serial

from firmware_fixture import NativeNode, packets

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('hk_bridge', REPO / 'tools/ground/hk-bridge.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


def main():
    image = Path(os.environ.get('KFSW_OUTPUT_ROOT', REPO.parent / 'build')) / 'linux/zephyr/zephyr.exe'
    with tempfile.TemporaryDirectory(prefix='kfsw-hk-class-') as directory:
        with NativeNode(image, Path(directory) / 'flight') as node:
            node.command('hk define 0 class=1 24:8', 'report 0 defines 1 values')
            node.command('hk define 1 class=6 24:12', 'report 1 defines 1 values')
            node.command('hk define 2 class=4 24:8', 'report 2 defines 1 values')
            node.command('hk collect 0', 'report 0 collected')
            node.command('hk collect 1', 'report 1 collected')
            node.command('hk collect 0', 'report 0 collected')
            node.command('hk collect 2', 'report 2 collected')
            with serial.Serial(node.device, 115200, timeout=.05) as ground:
                ground.write(bridge.build_request(16, 1, 23, 255, 8, 0, 0x42))
                ground.flush()
                received = [data for header, data in packets(ground, 2)
                            if header['source'] == 1 and header['destination'] == 16
                            and header['dport'] == 23]
                decoded = [struct.unpack('>BBHIBB', data[:10]) for data in received]
                assert [(row[1], row[2]) for row in decoded] == [(0, 1), (0, 0), (1, 0)], decoded
                assert [data[10:] for data in received] == [struct.pack('>I', 42),
                                                          struct.pack('>I', 42),
                                                          struct.pack('>i', -7)], received
                print('  [ok]   ground received mask 0x42 replies: report/sequence 0/1, 0/0, 1/0; values 42, 42, -7')
    print('HK CLASS SMOKE RESULT: PASS')


if __name__ == '__main__':
    main()
