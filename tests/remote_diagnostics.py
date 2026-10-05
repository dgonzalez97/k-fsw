"""Shared Linux/bench checks for CSP discovery through the host tool."""

import json
import subprocess


def check_remote_diagnostics(tool, node, send, read_log, output):
    def call(arguments, name, success=True):
        result = subprocess.run(tool + arguments, text=True, capture_output=True, timeout=10)
        (output / f'{name}.stdout').write_text(result.stdout)
        (output / f'{name}.stderr').write_text(result.stderr)
        assert (result.returncode == 0) == success, result
        return result

    def rows(path):
        return [json.loads(line) for line in path.read_text().splitlines()]

    # Remote logs are read from a K-FSW shell with log remote, not from the host tool.
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
