#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_INTEGRATION_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_INTEGRATION_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"

if ! command -v socat >/dev/null 2>&1; then
	echo "ERROR: socat is required for the software CSP/KISS integration test"
	exit 1
fi

# The temperature example rides along so the Yamcs bridge has a report worth
# pulling. native_sim has no die temperature, so it registers table 51 and
# reports the reserved value, which is the honest hosted answer.
KFSW_EXTRA_CONF_FILE="$KFSW_REPO_DIR/tests/config/param-fixtures.conf;$KFSW_REPO_DIR/config/profiles/linux-temperature.conf" \
	"$KFSW_CI_DIR/build.sh" linux
KFSW_PRISTINE=always "$KFSW_REPO_DIR/tests/build-linux-node2.sh"

echo "INTEGRATION: shell and local PARAM"
"$KFSW_REPO_DIR/tests/shell-smoke.sh"

echo "INTEGRATION: boton_test opt-in PARAM ownership"
"$KFSW_REPO_DIR/tests/boton-test-smoke.sh"

echo "INTEGRATION: storage"
"$KFSW_REPO_DIR/tests/storage-smoke.sh"

echo "INTEGRATION: PARAM persistence"
"$KFSW_REPO_DIR/tests/param-persistence-smoke.sh"

echo "INTEGRATION: housekeeping collection"
"$KFSW_REPO_DIR/tests/hk-smoke.sh"

echo "INTEGRATION: a procedure run from a file"
"$KFSW_REPO_DIR/tests/fbo-smoke.sh"

echo "INTEGRATION: housekeeping samples kept in a file"
"$KFSW_REPO_DIR/tests/hk-store-smoke.sh"

echo "INTEGRATION: the ground bridge agrees with the node about a sample"
"$KFSW_REPO_DIR/tests/hk-yamcs-smoke.sh"

echo "INTEGRATION: a node beacons and the ground hears it without asking"
"$KFSW_REPO_DIR/tests/hk-beacon-smoke.sh"

echo "INTEGRATION: CSP, remote PARAM, storage, and FTP"
"$KFSW_REPO_DIR/tests/csp-smoke.sh"

echo "INTEGRATION: multi-interface CSP/KISS routing and transit"
"$KFSW_REPO_DIR/tests/build-multi-kiss.sh"
"$KFSW_REPO_DIR/tests/multi-kiss-smoke.sh"

echo "INTEGRATION: k-ground UHF node 16 and ops node 19"
"$KFSW_REPO_DIR/tests/k-ground-csp-smoke.sh"

echo "INTEGRATION: k-ground file transfer between node 19 and node 16"
"$KFSW_REPO_DIR/tests/k-ground-ftp-smoke.sh"

echo
echo "INTEGRATION: firmware upload between two nodes"
"$KFSW_REPO_DIR/tests/k-ground-fwu-lite-smoke.sh"

echo
echo "INTEGRATION: firmware upload over a link that drops bytes"
"$KFSW_REPO_DIR/tests/k-ground-fwu-lite-smoke.sh" --lossy

echo
echo "INTEGRATION: firmware upload through the file transfer route"
"$KFSW_REPO_DIR/tests/k-ground-fwu-ftp-smoke.sh"

echo "INTEGRATION RESULT: PASS"
