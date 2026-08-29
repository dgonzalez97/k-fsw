#!/usr/bin/env bash
set -Eeuo pipefail

node=""

if [[ "${1:-}" == "--node" ]]; then
    if [[ $# -lt 2 ]]; then
        echo "ERROR: --node requires address 1 or 2"
        exit 1
    fi

    node="$2"
    shift 2
fi

case "${node:-1}" in
	1)
		source "$(dirname "$0")/_common.sh" linux
		build_command=("$KFSW_ROOT/k-fsw/tools/build.sh" linux)
		;;
	2)
		KFSW_BUILD_DIR="$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")/build/tests/linux-node2"
		source "$(dirname "$0")/_common.sh" linux
		build_command=("$KFSW_ROOT/k-fsw/tests/build-linux-node2.sh")
		;;
	*)
		echo "ERROR: supported Linux CSP nodes are 1 and 2"
		exit 1
		;;
esac

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"

if [[ ! -x "$executable" ]]; then
    echo "ERROR: KFSW-Linux has not been built."
    printf 'Run:'
    printf ' %q' "${build_command[@]}"
    printf '\n'
    exit 1
fi

has_flash_path=false
for argument in "$@"; do
	if [[ "$argument" == -flash=* ]]; then
		has_flash_path=true
		break
	fi
done

flash_arguments=()
if ! $has_flash_path; then
	flash_arguments+=("-flash=$KFSW_BUILD_DIR/kfsw-storage.bin")
fi

exec "$executable" "${flash_arguments[@]}" "$@"
