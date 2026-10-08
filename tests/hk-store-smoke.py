#!/usr/bin/env python3
"""Known HK dataset over two native_sim nodes and PTY KISS, not UART or RF."""

import binascii
from pathlib import Path
import struct
import sys
import tempfile
import time

from littlefs import LittleFS

sys.path.insert(0, str(Path(__file__).resolve().parent / 'hil/resources'))
from Ground import Ground


def main():
    with tempfile.TemporaryDirectory(prefix='kfsw-hk-store-') as directory:
        pair = Ground()
        try:
            pair.open_ground_pair(Path(directory) / 'pair')
            flight = pair.flight_command
            ground = pair.ground_command
            flight('hk define 0 24:8 24:12', 'report 0')
            flight('hk period 0 1000', 'report 0 every 1000 ms')
            flight('hk store 0 500', 'store for report 0: -34')
            print('  [ok]   an interval below the floor is refused')
            flight('hk store 0 10000', 'report 0 stores every 10000 ms')
            print('  [ok]   an interval at the floor is accepted')
            # Freeze known inputs for the first stored window, then stop the
            # scheduler before querying so the window cannot move.
            time.sleep(11)
            flight('hk period 0 0', 'report 0 every 0 ms')
            stored = flight('hk stored 0', 'held: seq')
            print('  [ok]   the stored window can be read back')
            flight('hk extract 0 /kfsw/hk/dataset.bin from=0 to=9', 'Extracted 10 records')
            flight('hk stored 0 from=1 to=1', 'selected: 1')
            print('  [ok]   a one-sequence window selects one sample')
            flight('hk stored 0 from=900', 'selected: 0')
            print('  [ok]   a window past the end selects nothing')
            ground('ftp ls 1 /hk', 'report0.bin')
            print('  [ok]   ftp ls names it a housekeeping file')
            ground('ftp mkdir 1 /hk/evil', '/hk/evil: FAIL (-30)')
            print('  [ok]   the ground cannot write under /hk')
            ground('ftp get 1 /hk/dataset.bin /build/dataset.bin', ': PASS')
            # Stop the ground before reading its downloaded LittleFS file.
            flash = pair.ground.flash
            pair.close_ground_pair()
            filesystem = LittleFS(block_size=4096, block_count=0x40000 // 4096,
                                  read_size=16, prog_size=16, cache_size=64, lookahead_size=32,
                                  mount=False)
            filesystem.context.buffer[:] = flash.read_bytes()[0xfc000:0x13c000]
            filesystem.mount()
            with filesystem.open('ftp/build/dataset.bin', 'rb') as handle:
                data = handle.read()
            magic, version, report, size, count, first, last, crc = struct.unpack('>4sBBHIHHI', data[:20])
            assert (magic, version, report, size, count, first, last) == (b'KHKD', 1, 0, 18, 10, 0, 9)
            assert len(data) == 20 + size * count
            assert binascii.crc32(data[:16] + bytes(4) + data[20:]) == crc
            for index in range(count):
                sample = data[20 + size * index:20 + size * (index + 1)]
                version, report, sequence, seconds, entries, flags, unsigned, signed = struct.unpack('>BBHIBBIi', sample)
                assert (version, report, sequence, entries, flags, unsigned, signed) == (1, 0, index, 2, 0, 42, -7)
            print('  [ok]   separate ground downloaded sequences 0..9 with values 42 and -7')
            # Reopen the existing flight store for the redefinition check.
            from firmware_fixture import NativeNode
            with NativeNode(pair.flight_image, Path(directory) / 'reopened', pair.flight.flash) as node:
                node.command('hk define 0 1:0', 'report 0')
                offset = node.send('ftp ls 1 /hk')
                node.wait('entries: 0', offset)
            print('  [ok]   a redefinition takes the file away')
        finally:
            pair.close_ground_pair()
    print('HK STORE SMOKE RESULT: PASS')


if __name__ == '__main__':
    main()
