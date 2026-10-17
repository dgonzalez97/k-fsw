#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_TARGET="${1:-nucleo_l496zg}"

source "$(dirname "$0")/../tools/_common.sh" "$KFSW_TARGET"

capture_file="$(mktemp)"
capture_pid=""
bootloader_file="$(mktemp)"

cleanup()
{
    if [[ -n "$capture_pid" ]]; then
        kill "$capture_pid" 2>/dev/null || true
        wait "$capture_pid" 2>/dev/null || true
    fi

    rm -f "$capture_file" "$bootloader_file"
}

trap cleanup EXIT

echo
echo "============================================================"
echo " K-FSW HARDWARE-IN-THE-LOOP SMOKE TEST"
echo "============================================================"
echo

echo "[1/5] ST-LINK"

if ! lsusb | grep -q "$KFSW_STLINK_USB_ID"; then
    echo "HIL RESULT: NOT RUN - $KFSW_STLINK_USB_ID not visible."
    exit 0
fi

echo "PASS"

echo
echo "[2/5] Serial"

if [[ ! -e "$KFSW_SERIAL" ]]; then
    echo "HIL RESULT: NOT RUN - $KFSW_SERIAL missing."
    exit 0
fi

echo "PASS"

echo
echo "[3/5] Build"

flash_args=()
if [[ "$KFSW_TARGET" == nucleo_l496zg ]]; then
    support="$ZEPHYR_BASE/boards/st/nucleo_l496zg/support"
    if ! openocd -s "$support" -f "$support/openocd.cfg" \
        -c "adapter serial $KFSW_STLINK_SERIAL" \
        -c "init" -c "halt" \
        -c "dump_image $bootloader_file 0x08000000 0x10000" \
        -c "resume" -c "shutdown"; then
        echo "FAIL: cannot read the bootloader area; no image was flashed."
        exit 1
    fi
    if [[ "$(wc -c <"$bootloader_file")" -ne 65536 ]]; then
        echo "FAIL: incomplete bootloader read; no image was flashed."
        exit 1
    fi
    if grep -aqF 'Booting MCUboot' "$bootloader_file"; then
        if [[ -z "${KFSW_MCUBOOT_KEY:-}" || ! -r "$KFSW_MCUBOOT_KEY" ]]; then
            echo "FAIL: MCUboot detected; set KFSW_MCUBOOT_KEY to its readable signing key."
            exit 1
        fi
        profiles="$KFSW_REPO_DIR/config/profiles"
        KFSW_BUILD_DIR="$KFSW_BUILD_DIR/boot-smoke-signed"
        export KFSW_BUILD_DIR
        echo "Boot image: signed application; MCUboot banner found in the bootloader area."
        KFSW_SYSBUILD=1 KFSW_PRISTINE=always \
            KFSW_EXTRA_CONF_FILE="$profiles/nucleo-mcuboot.conf;$profiles/nucleo-mcuboot-fwu.conf" \
            KFSW_EXTRA_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay;$profiles/nucleo-mcuboot.overlay;$profiles/nucleo-mcuboot-fwu.overlay" \
            KFSW_MCUBOOT_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay" \
            "$KFSW_REPO_DIR/tools/build.sh" "$KFSW_TARGET"
        flash_args+=(--domain app)
    else
        echo "Boot image: plain application; no MCUboot banner in the bootloader area."
        KFSW_PRISTINE=always "$KFSW_REPO_DIR/tools/build.sh" "$KFSW_TARGET"
    fi
else
    echo "Boot image: plain application; no MCUboot composition for $KFSW_TARGET."
    "$KFSW_REPO_DIR/tools/build.sh" "$KFSW_TARGET"
fi

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

kfsw_west_flash \
    -d "$KFSW_BUILD_DIR" \
    --runner openocd "${flash_args[@]}"

sleep 2

kill "$capture_pid" 2>/dev/null || true
wait "$capture_pid" 2>/dev/null || true
capture_pid=""

echo
echo "================ SERIAL ================="
cat "$capture_file"
echo "========================================="
echo

# The banner says which board answered. Two boards are on this bench and they
# are not the same composition, so @BOOT alone does not say this one booted.
if ! grep -Fq "board=$ZEPHYR_BOARD" "$capture_file"; then
    answered="$(sed -n 's/.*@BOOT .*board=\([^ ]*\).*/\1/p' "$capture_file" | head -1)"
    echo "HIL RESULT: FAIL"
    echo "  [x] ${answered:-nothing} answered on $KFSW_SERIAL, expected $ZEPHYR_BOARD"
    exit 1
fi

boot_ok=0
ready_ok=0

grep -Fq '@BOOT ' "$capture_file" && boot_ok=1
grep -Fq '@READY ' "$capture_file" && ready_ok=1

# The board stamps how long it took, so report it rather than only that it
# arrived. A ceiling turns it from a note into a number that can regress.
ready_ms="$(sed -n 's/.*@READY uptime_ms=\([0-9]\+\).*/\1/p' "$capture_file" | head -1)"
ceiling="${KFSW_BOOT_CEILING_MS:-2000}"

if [[ "$boot_ok" == 1 && "$ready_ok" == 1 ]]; then
    if [[ -n "$ready_ms" ]] && (( ready_ms > ceiling )); then
        echo "HIL RESULT: FAIL"
        echo "  [x] ready in ${ready_ms} ms, over the ${ceiling} ms ceiling"
        exit 1
    fi
    echo "HIL RESULT: PASS"
    echo "  [ok] @BOOT"
    echo "  [ok] @READY${ready_ms:+ in ${ready_ms} ms, ceiling ${ceiling} ms}"
    exit 0
fi

echo "HIL RESULT: FAIL"

[[ "$boot_ok" == 0 ]] && echo "  [x] @BOOT"
[[ "$ready_ok" == 0 ]] && echo "  [x] @READY"

exit 1
