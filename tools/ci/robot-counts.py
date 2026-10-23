#!/usr/bin/env python3
import os
import sys
import xml.etree.ElementTree as ET

board = len(sys.argv) == 3 and sys.argv[2] == '--board'
tests = list(ET.parse(sys.argv[1]).iter('test'))
statuses = [test.find('status').get('status') for test in tests]
skipped = statuses.count('SKIP')
failed = statuses.count('FAIL')
executed = len(statuses) - skipped
print(f'ROBOT COUNTS: executed={executed} skipped={skipped} failed={failed}')
if board:
    for test in tests:
        status = test.find('status')
        if status.get('status') == 'SKIP':
            reason = ' '.join(''.join(status.itertext()).split())
            print(f"NOT RUN: {test.get('name')}: {reason}")
acceptance = os.environ.get('KFSW_ROBOT_ACCEPTANCE', '0') == '1'
if failed or (not board and acceptance and (skipped or not executed)):
    sys.exit(1)
if board and not executed:
    print('NOT RUN: every case skipped' if tests else 'NOT RUN: no cases selected')
    sys.exit(3)
