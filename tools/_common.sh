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

export KFSW_BUILD_DIR="${KFSW_BUILD_DIR:-$KFSW_ROOT/build/$KFSW_TARGET}"

mkdir -p "$KFSW_ROOT/build"

cd "$KFSW_ROOT"
