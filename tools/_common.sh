#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_COMMON_FILE="$(readlink -f "${BASH_SOURCE[0]}")"

KFSW_TOOLS_DIR="$(dirname "$KFSW_COMMON_FILE")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"
KFSW_ROOT="$(dirname "$KFSW_REPO_DIR")"

KFSW_TARGET="${1:-nucleo_l496zg}"

KFSW_TARGET_FILE="$KFSW_REPO_DIR/config/targets/${KFSW_TARGET}.env"

if [[ ! -f "$KFSW_TARGET_FILE" ]]; then
    echo "ERROR: unsupported K-FSW target:"
    echo "  $KFSW_TARGET"
    exit 1
fi

source "$KFSW_TARGET_FILE"

KFSW_VENV_DIR="${KFSW_VENV_DIR:-$KFSW_ROOT/.venv}"

if [[ -f "$KFSW_VENV_DIR/bin/activate" ]]; then
    source "$KFSW_VENV_DIR/bin/activate"
fi

if ! command -v west >/dev/null 2>&1; then
    echo "ERROR: west is not available. Activate the Zephyr environment first."
    exit 1
fi

export KFSW_TARGET
export KFSW_ROOT
export ZEPHYR_BASE="$KFSW_ROOT/zephyr"

if [[ -z "${ZEPHYR_SDK_INSTALL_DIR:-}" && \
      -d "$HOME/zephyr-sdk-1.0.1" ]]; then
    export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-1.0.1"
fi

if [[ -n "${ZEPHYR_SDK_INSTALL_DIR:-}" ]]; then
    export PATH="$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin:$PATH"
fi

export KFSW_BUILD_DIR="${KFSW_BUILD_DIR:-${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/$KFSW_TARGET}"

# Flash the board this target names. With two probes attached openocd takes the
# first it finds, which is a coin flip over which board gets the image, so a
# target profile that has an ST-Link names it and this passes it on.
kfsw_west_flash()
{
    local flash_args=("$@")

    if [[ -n "${KFSW_STLINK_SERIAL:-}" ]]; then
        flash_args+=(--cmd-pre-init "adapter serial $KFSW_STLINK_SERIAL")
    fi

    west flash "${flash_args[@]}"
}

# Nothing capped compilation, so a 16-core desktop ran 16 Twister builds of 16
# compilers each and the machine spent the run swapping with the fan at full
# tilt. Leave half the cores to the person using the computer; KFSW_JOBS
# overrides it, and 0 means take everything.
if [[ -z "${KFSW_JOBS:-}" ]]; then
	KFSW_JOBS="$(( $(nproc 2>/dev/null || echo 2) / 2 ))"
	[[ "$KFSW_JOBS" -ge 1 ]] || KFSW_JOBS=1
fi
export KFSW_JOBS
if [[ "$KFSW_JOBS" != "0" ]]; then
	export CMAKE_BUILD_PARALLEL_LEVEL="$KFSW_JOBS"
fi

mkdir -p "${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}"

cd "$KFSW_ROOT"
