#!/usr/bin/env python3
"""Ground operator requests over hosted PTY KISS; software evidence only."""

from pathlib import Path
import re
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent / 'hil/resources'))
from Ground import Ground


def check(pair):
    ground = pair.ground_command
    flight = pair.flight_command

    ground('param get 1 ground_wtd_timeout', '1:ground_wtd_timeout = 86400')
    ground('param set 1 ground_wtd_timeout 8000', '1:ground_wtd_timeout = 8000')
    ground('param get 1 ground_wtd_timeout', '1:ground_wtd_timeout = 8000')
    flight('param get ground_wtd_timeout', 'ground_wtd_timeout = 8000')
    print('  [ok] remote PARAM write and fresh read agree with the serving node')

    ground('param get 1 node_id', '1:node_id = 1')
    ground('param set 1 node_id 7', "set: parameter 'node_id' is read-only")
    ground('param get 1 node_id', '1:node_id = 1')
    print('  [ok] read-only refusal names node_id and leaves it unchanged')

    refused = ground('param set 1 ground_wtd_timeout 7199', 'param set: node 1 did not answer')
    assert '1:ground_wtd_timeout = 7199' not in refused, 'rejected write reported success'
    ground('csp ping 1', 'CSP ping 1: success')
    ground('param get 1 ground_wtd_timeout', '1:ground_wtd_timeout = 8000')
    flight('param get ground_wtd_timeout', 'ground_wtd_timeout = 8000')
    print('  [ok] out-of-range push refused; link usable and previous value preserved')

    ground('hk define 1 0 1:0', 'report 0 defines 1 values')
    ground('hk period 1 0 1000', 'report 0 every 1000 ms')
    deadline = time.monotonic() + 5
    while True:
        sample = flight('hk get 0', '')
        if re.search(r'seq \d+ .* 1 values .* 12 bytes', sample):
            break
        assert time.monotonic() < deadline, 'remote report 0 did not collect within 5 seconds'
        time.sleep(.2)
    ground('hk period 1 0 0', 'report 0 every 0 ms')
    sample = flight('hk get 0', '1 values')
    assert re.search(r'(?m)^\s+0001\s*$', sample), 'report 0 payload is not remote node_id=1'
    print('  [ok] remote HK definition and scheduling collect node_id=1 (flight-console readback)')

    ground('hk clear 1 0', 'report 0 cleared')
    ground('hk period 1 0 1000', 'hk_period: failed, period for report 0: -2')
    ground('csp ping 1', 'CSP ping 1: success')
    print('  [ok] absent report refused with ENOENT; the link still answers')


def main():
    with tempfile.TemporaryDirectory(prefix='kfsw-ground-services.') as directory:
        pair = Ground()
        try:
            pair.open_ground_pair(str(Path(directory) / 'pair'))
            check(pair)
        except BaseException:
            print('GROUND SERVICES RESULT: FAIL')
            if hasattr(pair, 'output'):
                for log in sorted(pair.output.glob('*/node.log')):
                    print(f'--- {log.parent.name} ---\n{log.read_text()}')
            raise
        finally:
            pair.close_ground_pair()
    print('GROUND SERVICES RESULT: PASS')


if __name__ == '__main__':
    main()
