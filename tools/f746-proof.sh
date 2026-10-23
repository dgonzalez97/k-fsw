#!/usr/bin/env bash
set -Eeuo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
usage() {
    cat <<'HELP'
Usage: tools/f746-proof.sh build|check [output-directory]
Build-only F746 A/permanent Golden proof. No flash command.
Default output: <repository>/build/f746-proof
KFSW_MCUBOOT_KEY: existing signing key (default ~/.config/kfsw/mcuboot-signing-key.pem).
The build system signs; this tool never reads or copies the private key.
check uses generated artifacts only; build runs check after both builds.
HELP
}
case "${1:---help}" in
    --help|-h) usage; exit 0 ;;
    build|check) action="$1" ;;
    *) usage >&2; exit 2 ;;
esac
[[ $# -le 2 ]] || { usage >&2; exit 2; }
out="$(realpath -m "${2:-$repo/build/f746-proof}")"
if [[ "$action" == build ]]; then
    export KFSW_MCUBOOT_KEY="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}"
    [[ -r "$KFSW_MCUBOOT_KEY" ]] || { echo 'ERROR: set KFSW_MCUBOOT_KEY to an existing readable signing key' >&2; exit 1; }
    p="$repo/config/profiles"
    export KFSW_SYSBUILD=1 KFSW_PRISTINE=always
    export KFSW_MCUBOOT_DTC_OVERLAY_FILE="$p/f746-recovery-flash.overlay"
    export KFSW_CMAKE_ARGS="-DSB_CONFIG_MCUBOOT_MODE_FIRMWARE_UPDATER=y -Dmcuboot_EXTRA_CONF_FILE=$p/f746-mcuboot.conf"
    for image in a golden; do
        export KFSW_BUILD_DIR="$out/$image"
        export KFSW_EXTRA_CONF_FILE="$p/f746-uart.conf;$p/f746-can.conf;$p/f746-minimal.conf"
        overlay=f746-recovery.overlay
        if [[ "$image" == golden ]]; then
            KFSW_EXTRA_CONF_FILE+=";$p/f746-golden.conf"
            overlay=f746-golden.overlay
        fi
        export KFSW_EXTRA_DTC_OVERLAY_FILE="$p/f746-recovery-flash.overlay;$p/$overlay;$p/f746-uart.overlay;$p/f746-can.overlay"
        "$repo/tools/build.sh" nucleo_f746zg
    done
fi
exec python3 "$repo/tools/f746-validate.py" "$out"
