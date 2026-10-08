#!/usr/bin/env bash
# Two hosted nodes over PTY KISS; no physical UART wiring or RF evidence.
set -euo pipefail
repo="$(dirname "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")")"
python="$repo/../.venv/bin/python"
[[ -x "$python" ]] || python=python3
exec "$python" "$repo/tests/hk-store-smoke.py" "$@"
