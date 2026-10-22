#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

work_dir="$(mktemp -d /tmp/kfsw-gndwdt-expiry.XXXXXX)"
node_pid=""
cleanup()
{
	[[ -n "$node_pid" ]] && kill "$node_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	rm -rf -- "$work_dir"
}
trap cleanup EXIT

fail()
{
	echo "GNDWDT EXPIRY RESULT: FAIL - $1"
	cat "$work_dir/node.log"
	exit 1
}

build_dir="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/tests/linux-gndwdt-expiry"
KFSW_BUILD_DIR="$build_dir" \
	KFSW_EXTRA_CONF_FILE="$KFSW_REPO_DIR/tests/config/gndwdt-expiry.conf" \
	"$KFSW_REPO_DIR/tools/build.sh" linux

printf 'gndwdt show\n' >"$work_dir/input"
"$build_dir/zephyr/zephyr.exe" --uart_stdinout --no-color --no-rt \
	-flash="$work_dir/flash.bin" <"$work_dir/input" >"$work_dir/node.log" 2>&1 &
node_pid=$!

for _ in {1..400}; do
	boots="$(grep -c '@READY ' "$work_dir/node.log" || true)"
	[[ "$boots" -ge 2 ]] && break
	kill -0 "$node_pid" 2>/dev/null || fail "node exited before rebooting"
	sleep 0.05
done

for expected in \
	'state: armed' \
	'contacts: 0 last_node: 0' \
	'Ground watchdog: no contact for 7200 s; resetting' \
	'reset_cause=software'; do
	grep -Fq "$expected" "$work_dir/node.log" || fail "missing: $expected"
done
[[ "$boots" -ge 2 ]] || fail "watchdog expiry did not reach a second ready boot within 20 seconds"

python3 - "$work_dir/node.log" <<'PYTEST'
from pathlib import Path
import re
import sys
text = Path(sys.argv[1]).read_text()
assert re.search(r'Ground watchdog: no contact for 7200 s; resetting.*?'
                 r'@BOOT [^\n]*reset_cause=software.*?@READY ', text, re.S), \
    'watchdog expiry was not followed by a ready software-reset boot'
PYTEST

grep -E '@BOOT |@READY |Ground watchdog: no contact|contacts:|state:' "$work_dir/node.log"
echo "GNDWDT EXPIRY RESULT: PASS"
