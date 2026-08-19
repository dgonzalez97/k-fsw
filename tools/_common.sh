#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_COMMON_FILE="$(readlink -f "${BASH_SOURCE[0]}")"

KFSW_TOOLS_DIR="$(dirname "$KFSW_COMMON_FILE")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"
KFSW_ROOT="$(dirname "$KFSW_REPO_DIR")"

KFSW_PROFILE="${1:-nucleo_l496zg}"

KFSW_PROFILE_FILE="$KFSW_REPO_DIR/config/boards/${KFSW_PROFILE}.env"

if [[ ! -f "$KFSW_PROFILE_FILE" ]]; then
    echo "ERROR: unknown K-FSW profile:"
    echo "  $KFSW_PROFILE"
    exit 1
fi

source "$KFSW_PROFILE_FILE"
source "$KFSW_ROOT/.venv/bin/activate"

export KFSW_PROFILE
export KFSW_ROOT
export ZEPHYR_BASE="$KFSW_ROOT/zephyr"

export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-$HOME/zephyr-sdk-1.0.1}"

export PATH="$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin:$PATH"

export KFSW_BUILD_DIR="$KFSW_ROOT/build/$KFSW_PROFILE"

mkdir -p "$KFSW_ROOT/build"

cd "$KFSW_ROOT"
