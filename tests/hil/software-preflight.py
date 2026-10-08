#!/usr/bin/env python3
"""Acceptance prerequisites for hosted PTY KISS and vcan; no physical evidence."""

import importlib
import os
import shutil
import subprocess
import sys


def main():
    errors = []
    for module in ('robot', 'serial', 'littlefs', 'elftools'):
        try:
            importlib.import_module(module)
        except ImportError:
            errors.append(f'missing Python module: {module}')
    for tool in ('socat', 'tmux', 'ip'):
        if not shutil.which(tool):
            errors.append(f'missing command: {tool}')
    for variable in ('KFSW_CSP_TOOLS_TEST_BINARY', 'KFSW_CSP_IPERF_TEST_BINARY'):
        if not os.access(os.environ.get(variable, ''), os.X_OK):
            errors.append(f'missing executable: {variable}')
    if shutil.which('ip'):
        result = subprocess.run(['ip', 'link', 'show', 'vcan0'], capture_output=True)
        if result.returncode:
            errors.append('missing vcan0; run tests/vcan-up.sh')
    for error in errors:
        print(f'ROBOT PREFLIGHT: {error}', file=sys.stderr)
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
