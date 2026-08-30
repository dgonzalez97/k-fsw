#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_INTEGRATION_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_INTEGRATION_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"

if ! command -v socat >/dev/null 2>&1; then
	echo "ERROR: socat is required for the software CSP/KISS integration test"
	exit 1
fi

"$KFSW_CI_DIR/build.sh" linux
KFSW_PRISTINE=always "$KFSW_REPO_DIR/tests/build-linux-node2.sh"

echo "INTEGRATION: shell and local PARAM"
"$KFSW_REPO_DIR/tests/shell-smoke.sh"

echo "INTEGRATION: storage"
"$KFSW_REPO_DIR/tests/storage-smoke.sh"

echo "INTEGRATION: PARAM persistence"
"$KFSW_REPO_DIR/tests/param-persistence-smoke.sh"

echo "INTEGRATION: CSP, remote PARAM, storage, and FTP"
"$KFSW_REPO_DIR/tests/csp-smoke.sh"

echo "INTEGRATION: k-ground nodes 16 and 17"
"$KFSW_REPO_DIR/tests/k-ground-csp-smoke.sh"

echo "INTEGRATION RESULT: PASS"
