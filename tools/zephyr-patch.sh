#!/usr/bin/env bash
set -Eeuo pipefail

repo_dir="$(dirname "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")")"
workspace="$repo_dir"
while [[ "$workspace" != / && ! -d "$workspace/.west" ]]; do
	workspace="$(dirname "$workspace")"
done
if [[ "$workspace" == / ]]; then
	workspace="$(dirname "$repo_dir")"
fi

mode=apply
if [[ "${1:-}" == --check ]]; then
	mode=check
	shift
fi
if [[ $# -gt 1 || "${1:-}" == -* ]]; then
	echo "Usage: $0 [--check] [zephyr-directory]" >&2
	exit 2
fi
zephyr_dir="${1:-$workspace/zephyr}"
patch_file="$repo_dir/patches/zephyr/shell-tab-help.patch"

if [[ ! -d "$zephyr_dir" ]]; then
	echo "ERROR: Zephyr directory not found: $zephyr_dir" >&2
	exit 1
fi
if [[ ! -f "$zephyr_dir/subsys/shell/shell.c" || ! -r "$zephyr_dir/subsys/shell/shell.c" ]]; then
	echo "ERROR: Zephyr shell source not found or unreadable: $zephyr_dir/subsys/shell/shell.c" >&2
	exit 1
fi
if [[ ! -f "$patch_file" || ! -r "$patch_file" ]]; then
	echo "ERROR: Cannot read $patch_file" >&2
	exit 1
fi

diagnostic=''
check_patch()
{
	local status
	if diagnostic=$(git -C "$zephyr_dir" apply --check "$@" "$patch_file" 2>&1); then
		return 0
	else
		status=$?
	fi
	if [[ "$status" != 1 ]]; then
		printf '%s\n' "$diagnostic" >&2
		echo "ERROR: git could not check the Zephyr shell Tab help patch in $zephyr_dir" >&2
		exit 1
	fi
	return 1
}

if check_patch --reverse; then
	echo 'Zephyr shell Tab help patch is already applied.'
	exit 0
fi

if ! check_patch; then
	printf '%s\n' "$diagnostic" >&2
	echo "ERROR: $zephyr_dir/subsys/shell/shell.c conflicts with the Zephyr shell Tab help patch." >&2
	echo "Inspect it with: git -C \"$zephyr_dir\" diff -- subsys/shell/shell.c" >&2
	echo "If it carries an earlier revision of this patch, restore it with:" >&2
	echo "git -C \"$zephyr_dir\" checkout -- subsys/shell/shell.c" >&2
	echo 'and run this helper again.' >&2
	exit 1
fi
if [[ "$mode" == check ]]; then
	echo 'ERROR: Zephyr shell Tab help patch is not applied.' >&2
	echo 'Apply it from the workspace root: ./k-fsw/tools/zephyr-patch.sh' >&2
	exit 1
fi

if ! git -C "$zephyr_dir" apply "$patch_file"; then
	echo "ERROR: git could not write the Zephyr shell Tab help patch into $zephyr_dir" >&2
	exit 1
fi
echo 'Zephyr shell Tab help patch applied.'
