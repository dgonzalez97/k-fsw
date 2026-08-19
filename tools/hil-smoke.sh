#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_PROFILE="${1:-nucleo_l496zg}"

source "$(dirname "$0")/_common.sh" "$KFSW_PROFILE"

capture_file="$(mktemp)"
capture_pid=""

cleanup()
{
    if [[ -n "$capture_pid" ]]; then
        kill "$capture_pid" 2>/dev/null || true
        wait "$capture_pid" 2>/dev/null || true
    fi

    rm -f "$capture_file"
}

trap cleanup EXIT

echo
echo "============================================================"
echo " K-FSW HARDWARE-IN-THE-LOOP SMOKE TEST"
echo "============================================================"
echo

echo "[1/5] ST-LINK"

if ! lsusb | grep -q "$KFSW_STLINK_USB_ID"; then
    echo "FAIL: $KFSW_STLINK_USB_ID not visible."
    exit 1
fi

echo "PASS"

echo
echo "[2/5] Serial"

if [[ ! -e "$KFSW_SERIAL" ]]; then
    echo "FAIL: $KFSW_SERIAL missing."
    exit 1
fi

echo "PASS"

echo
echo "[3/5] Build"

"$KFSW_ROOT/k-fsw/tools/build.sh" "$KFSW_PROFILE"

echo
echo "[4/5] Start serial capture BEFORE reset"

stty -F "$KFSW_SERIAL" \
    "$KFSW_SERIAL_BAUD" \
    cs8 \
    -cstopb \
    -parenb \
    raw \
    -echo

timeout 15s cat "$KFSW_SERIAL" >"$capture_file" &
capture_pid=$!

sleep 0.25

echo
echo "[5/5] Flash / reset / boot"

west flash \
    -d "$KFSW_BUILD_DIR" \
    --runner openocd

sleep 2

kill "$capture_pid" 2>/dev/null || true
wait "$capture_pid" 2>/dev/null || true
capture_pid=""

echo
echo "================ SERIAL ================="
cat "$capture_file"
echo "========================================="
echo

boot_ok=0
ready_ok=0

grep -Fq '@BOOT ' "$capture_file" && boot_ok=1
grep -Fq '@READY ' "$capture_file" && ready_ok=1

if [[ "$boot_ok" == 1 && "$ready_ok" == 1 ]]; then
    echo "HIL RESULT: PASS"
    echo "  [✓] @BOOT"
    echo "  [✓] @READY"
    exit 0
fi

echo "HIL RESULT: FAIL"

[[ "$boot_ok" == 0 ]] && echo "  [x] @BOOT"
[[ "$ready_ok" == 0 ]] && echo "  [x] @READY"

exit 1
