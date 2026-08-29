#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_INTELLISENSE_TOOLS_DIR="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
KFSW_INTELLISENSE_REPO_DIR="$(dirname "$KFSW_INTELLISENSE_TOOLS_DIR")"
KFSW_INTELLISENSE_ROOT="$(dirname "$KFSW_INTELLISENSE_REPO_DIR")"
KFSW_INTELLISENSE_TARGET="${1:-linux}"

case "$KFSW_INTELLISENSE_TARGET" in
    linux|nucleo_l496zg)
        ;;
    *)
        echo "ERROR: unsupported IntelliSense target:"
        echo "  $KFSW_INTELLISENSE_TARGET"
        echo "Supported targets: linux, nucleo_l496zg"
        exit 1
        ;;
esac

KFSW_INTELLISENSE_DATABASE="$KFSW_INTELLISENSE_ROOT/build/$KFSW_INTELLISENSE_TARGET/compile_commands.json"
KFSW_INTELLISENSE_ACTIVE_DATABASE="$KFSW_INTELLISENSE_ROOT/build/compile_commands.json"

if [[ ! -f "$KFSW_INTELLISENSE_DATABASE" ]]; then
    echo "ERROR: compilation database not found:"
    echo "  $KFSW_INTELLISENSE_DATABASE"
    echo "Generate it with:"
    echo "  $KFSW_INTELLISENSE_REPO_DIR/tools/build.sh $KFSW_INTELLISENSE_TARGET"
    exit 1
fi

if [[ -e "$KFSW_INTELLISENSE_ACTIVE_DATABASE" && \
      ! -L "$KFSW_INTELLISENSE_ACTIVE_DATABASE" ]]; then
    echo "ERROR: refusing to replace a non-symlink compilation database:"
    echo "  $KFSW_INTELLISENSE_ACTIVE_DATABASE"
    exit 1
fi

ln -sfn "$KFSW_INTELLISENSE_TARGET/compile_commands.json" \
    "$KFSW_INTELLISENSE_ACTIVE_DATABASE"

echo "K-FSW IntelliSense target: $KFSW_INTELLISENSE_TARGET"
echo "Compilation database: $KFSW_INTELLISENSE_DATABASE"
echo "Reset the C/C++ IntelliSense database after changing targets."
