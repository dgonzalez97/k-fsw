#!/usr/bin/env bash
set -Eeuo pipefail

node=""
profile=""

if [[ "${1:-}" == "--profile" ]]; then
	if [[ $# -lt 2 ]]; then
		echo "ERROR: --profile requires a Linux profile name"
		exit 1
	fi

	profile="$2"
	shift 2
elif [[ "${1:-}" == "--node" ]]; then
    if [[ $# -lt 2 ]]; then
        echo "ERROR: --node requires address 1 or 2"
        exit 1
    fi

    node="$2"
    shift 2
fi

if [[ -z "$profile" ]]; then
	case "${node:-1}" in
		1)
			profile=linux
			;;
		2)
			profile=linux_node2
			;;
		*)
			echo "ERROR: supported Linux CSP nodes are 1 and 2"
			exit 1
			;;
	esac
fi

if [[ "$profile" != linux && "$profile" != linux_node2 &&
	  "$profile" != linux_uart ]]; then
	echo "ERROR: unsupported Linux profile: $profile"
	exit 1
fi

source "$(dirname "$0")/_common.sh" "$profile"

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"

if [[ ! -x "$executable" ]]; then
    echo "ERROR: KFSW-Linux has not been built."
    echo "Run: $KFSW_ROOT/k-fsw/tools/build.sh $profile"
    exit 1
fi

exec "$executable" "$@"
