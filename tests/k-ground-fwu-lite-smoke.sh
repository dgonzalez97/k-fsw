#!/usr/bin/env bash
set -Eeuo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python="${KFSW_PYTHON:-$repo/../.venv/bin/python}"
[[ -x "$python" ]] || python=python3
exec "$python" "$repo/tests/fwu-smoke.py" --protocol lite "$@"
