#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_INTEGRATION_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_INTEGRATION_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"

if ! command -v socat >/dev/null 2>&1; then
	echo "ERROR: socat is required for the software CSP/KISS integration test"
	exit 1
fi

# A workspace checkout keeps its tools in .venv; hosted CI installs them into
# the job's own Python instead.
kfsw_python="${KFSW_PYTHON:-}"
if [[ -z "$kfsw_python" ]]; then
	if [[ -x "$KFSW_REPO_DIR/../.venv/bin/python" ]]; then
		kfsw_python="$KFSW_REPO_DIR/../.venv/bin/python"
	else
		kfsw_python="python3"
	fi
fi

# Include the temperature example so the Yamcs bridge has a report to pull.
# native_sim has no sensor, so it reports the reserved value.
KFSW_EXTRA_CONF_FILE="$KFSW_REPO_DIR/tests/config/param-fixtures.conf;$KFSW_REPO_DIR/config/profiles/linux-temperature.conf" \
	"$KFSW_CI_DIR/build.sh" linux
KFSW_PRISTINE=always "$KFSW_REPO_DIR/tests/build-linux-node2.sh"

echo "INTEGRATION: ground tools and HK capture/replay"
"$kfsw_python" -m unittest discover -s "$KFSW_REPO_DIR/tests/ground" -v
echo "INTEGRATION: explicit ground watchdog command"
gndwdt_output="$(mktemp -d /tmp/kfsw-gndwdt.XXXXXX)"
"$kfsw_python" "$KFSW_REPO_DIR/tests/gndwdt-smoke.py" \
	--executable "${KFSW_OUTPUT_ROOT:-$KFSW_REPO_DIR/../build}/linux/zephyr/zephyr.exe" \
	--output "$gndwdt_output/run"
rm -rf -- "$gndwdt_output"
echo "INTEGRATION: shell and local PARAM"
"$KFSW_REPO_DIR/tests/shell-smoke.sh"

echo "INTEGRATION: boton_test parameters"
"$KFSW_REPO_DIR/tests/boton-test-smoke.sh"

echo "INTEGRATION: storage"
"$KFSW_REPO_DIR/tests/storage-smoke.sh"

echo "INTEGRATION: PARAM persistence"
"$KFSW_REPO_DIR/tests/param-persistence-smoke.sh"

echo "INTEGRATION: a parameter table uploaded as a file"
"$KFSW_REPO_DIR/tests/table-file-smoke.sh"

echo "INTEGRATION: housekeeping collection"
"$KFSW_REPO_DIR/tests/hk-smoke.sh"

echo "INTEGRATION: the ground keeps what came down"
"$kfsw_python" "$KFSW_REPO_DIR/tests/hk-ground-store-smoke.py"

echo "INTEGRATION: a procedure run from a file"
"$KFSW_REPO_DIR/tests/fbo-smoke.sh"

echo "INTEGRATION: multi-interface CSP/KISS routing and transit"
"$KFSW_REPO_DIR/tests/build-multi-kiss.sh"
"$KFSW_REPO_DIR/tests/multi-kiss-smoke.sh"

echo "INTEGRATION RESULT: PASS"
