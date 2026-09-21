"""Shared Linux/bench checks for remote logs and CSP discovery."""

import json
import subprocess
import time


def check_remote_diagnostics(tool, node, send, read_log, output):
    def call(arguments, name, success=True):
        result = subprocess.run(tool + arguments, text=True, capture_output=True, timeout=10)
        (output / f'{name}.stdout').write_text(result.stdout)
        (output / f'{name}.stderr').write_text(result.stderr)
        assert (result.returncode == 0) == success, result
        return result

    def rows(path):
        return [json.loads(line) for line in path.read_text().splitlines()]

    previous = read_log().count('[ERROR] K-FSW shell log test: error')
    send('log test')
    deadline = time.monotonic() + 5
    while read_log().count('[ERROR] K-FSW shell log test: error') <= previous:
        assert time.monotonic() < deadline, 'log test did not produce a new error'
        time.sleep(.05)
    first = output / 'logs.jsonl'
    call(['logs', '--node', str(node), '--count', '3', '--output', str(first)], 'logs')
    capture = rows(first)
    records = [row for row in capture if row['kind'] == 'log']
    assert capture[0]['kind'] == 'start' and capture[-1]['complete'], capture
    assert [row['severity'] for row in records] == [3, 2, 1], records
    assert all('K-FSW shell log test:' in row['text'] for row in records)
    assert all(bytes.fromhex(row['text_hex']).decode() == row['text'] for row in records)
    second = output / 'logs-repeat.jsonl'
    call(['logs', '--node', str(node), '--count', '3', '--output', str(second)], 'logs-repeat')
    assert [row for row in rows(second) if row['kind'] == 'log'] == records
    filtered = call(['logs', '--node', str(node), '--count', '3', '--min-level', '2'], 'logs-filtered')
    assert [row['severity'] for row in map(json.loads, filtered.stdout.splitlines())
            if row['kind'] == 'log'] == [3, 2]
    call(['logs', '--node', str(node), '--output', str(first)], 'logs-existing', success=False)
    assert rows(first) == capture, 'existing capture was modified'
    call(['--timeout-ms', '100', 'logs', '--node', '100'], 'logs-absent', success=False)

    for _ in range(12):
        send('log test')
        time.sleep(.03)
    deadline = time.monotonic() + 5
    while read_log().count('[ERROR] K-FSW shell log test: error') < previous + 13:
        assert time.monotonic() < deadline, 'log wrap fixture did not finish'
        time.sleep(.05)
    wrap = call(['logs', '--node', str(node)], 'logs-wrap')
    wrapped = [json.loads(line) for line in wrap.stdout.splitlines()]
    assert wrapped[0]['overwritten'] > 0 and wrapped[-1]['complete'], wrapped

    inventory = output / 'discover.jsonl'
    call(['--timeout-ms', '100', 'discover', '--nodes', f'{node},100',
          '--output', str(inventory)], 'discover')
    nodes = rows(inventory)
    found = next(row for row in nodes if row.get('node') == node)
    assert found['status'] == 'identified' and found['identity']['hostname'], found
    assert next(row for row in nodes if row.get('node') == 100)['status'] == 'no_reply'
    assert nodes[-1]['complete'], nodes
    call(['discover', '--nodes', str(node), '--output', str(inventory)], 'discover-existing', success=False)
    assert rows(inventory) == nodes
    call(['discover', '--range', '0:16382'], 'discover-range', success=False)
    call(['discover', '--nodes', '16'], 'discover-source', success=False)
    partial = call(['--timeout-ms', '200', 'discover', '--nodes', '100,101', '--budget-ms', '30'],
                   'discover-budget', success=False)
    partial_rows = [json.loads(line) for line in partial.stdout.splitlines()]
    assert partial_rows[1]['status'] == 'not_queried' and not partial_rows[-1]['complete']
    print('REMOTE DIAGNOSTICS RESULT: PASS')
