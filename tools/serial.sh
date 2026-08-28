#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_TARGET="${1:-nucleo_l496zg}"
DURATION_SECONDS="${2:-10}"

source "$(dirname "$0")/_common.sh" "$KFSW_TARGET"

if [[ ! -e "$KFSW_SERIAL" ]]; then
    echo "ERROR: serial device not found: $KFSW_SERIAL"
    exit 1
fi

stty -F "$KFSW_SERIAL" \
    "$KFSW_SERIAL_BAUD" \
    cs8 \
    -cstopb \
    -parenb \
    raw \
    -echo

echo
echo "Capturing $KFSW_SERIAL @ $KFSW_SERIAL_BAUD for ${DURATION_SECONDS}s"
echo

set +e
timeout "${DURATION_SECONDS}s" cat "$KFSW_SERIAL"
RC=$?
set -e

echo

if [[ "$RC" != 0 && "$RC" != 124 ]]; then
    exit "$RC"
fi
