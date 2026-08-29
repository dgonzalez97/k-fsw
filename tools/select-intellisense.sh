#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_INTELLISENSE_TOOLS_DIR="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
KFSW_INTELLISENSE_REPO_DIR="$(dirname "$KFSW_INTELLISENSE_TOOLS_DIR")"
KFSW_INTELLISENSE_ROOT="$(dirname "$KFSW_INTELLISENSE_REPO_DIR")"
KFSW_INTELLISENSE_TARGET="${1:-linux}"
KFSW_INTELLISENSE_SETTINGS_TEMPLATE="$KFSW_INTELLISENSE_REPO_DIR/tools/vscode/active-build-settings.json"

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

KFSW_INTELLISENSE_BUILD_DIR="$KFSW_INTELLISENSE_ROOT/build/$KFSW_INTELLISENSE_TARGET"
KFSW_INTELLISENSE_DATABASE="$KFSW_INTELLISENSE_BUILD_DIR/compile_commands.json"
KFSW_INTELLISENSE_ACTIVE_DATABASE="$KFSW_INTELLISENSE_ROOT/build/compile_commands.json"
KFSW_INTELLISENSE_ACTIVE_BUILD="$KFSW_INTELLISENSE_ROOT/build/active"
KFSW_INTELLISENSE_BUILD_SETTINGS_DIR="$KFSW_INTELLISENSE_BUILD_DIR/.vscode"
KFSW_INTELLISENSE_BUILD_SETTINGS="$KFSW_INTELLISENSE_BUILD_SETTINGS_DIR/settings.json"

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

if [[ -e "$KFSW_INTELLISENSE_ACTIVE_BUILD" && \
      ! -L "$KFSW_INTELLISENSE_ACTIVE_BUILD" ]]; then
    echo "ERROR: refusing to replace a non-symlink active build:"
    echo "  $KFSW_INTELLISENSE_ACTIVE_BUILD"
    exit 1
fi

if [[ -e "$KFSW_INTELLISENSE_BUILD_SETTINGS" && \
      ! -L "$KFSW_INTELLISENSE_BUILD_SETTINGS" ]]; then
    echo "ERROR: refusing to replace non-symlink build folder settings:"
    echo "  $KFSW_INTELLISENSE_BUILD_SETTINGS"
    exit 1
fi

mkdir -p "$KFSW_INTELLISENSE_BUILD_SETTINGS_DIR"

KFSW_INTELLISENSE_SETTINGS_LINK="$(realpath \
    --relative-to="$KFSW_INTELLISENSE_BUILD_SETTINGS_DIR" \
    "$KFSW_INTELLISENSE_SETTINGS_TEMPLATE")"

ln -sfn "$KFSW_INTELLISENSE_SETTINGS_LINK" \
    "$KFSW_INTELLISENSE_BUILD_SETTINGS"
ln -sfn "$KFSW_INTELLISENSE_TARGET/compile_commands.json" \
    "$KFSW_INTELLISENSE_ACTIVE_DATABASE"
ln -sfn "$KFSW_INTELLISENSE_TARGET" "$KFSW_INTELLISENSE_ACTIVE_BUILD"

echo "K-FSW IntelliSense target: $KFSW_INTELLISENSE_TARGET"
echo "Compilation database: $KFSW_INTELLISENSE_DATABASE"
echo "Active build: $KFSW_INTELLISENSE_BUILD_DIR"
echo "Refresh the VS Code Explorer after changing targets."
echo "Reset the C/C++ IntelliSense database after changing targets."
