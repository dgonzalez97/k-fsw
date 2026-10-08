#!/usr/bin/env bash
# Hosted PTY KISS only; no physical claim.
set -euo pipefail
repo="$(dirname "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")")"
python="$repo/../.venv/bin/python"
[[ -x "$python" ]] || python=python3
exec "$python" "$repo/tests/hk-class-smoke.py" "$@"
