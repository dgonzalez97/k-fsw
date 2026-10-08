#!/usr/bin/env bash
# Passive ground receives native_sim PTY KISS; no UART wiring or RF evidence.
set -euo pipefail
repo="$(dirname "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")")"
python="$repo/../.venv/bin/python"
[[ -x "$python" ]] || python=python3
exec "$python" "$repo/tests/hk-beacon-smoke.py" "$@"
