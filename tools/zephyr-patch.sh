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
if [[ ! -d "$zephyr_dir" ]]; then
	echo "ERROR: Zephyr directory not found: $zephyr_dir" >&2
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
		echo "ERROR: git could not check the Zephyr $patch_name patch in $zephyr_dir" >&2
		exit 1
	fi
	return 1
}

apply_patch()
{
	if check_patch --reverse; then
		echo "Zephyr $patch_name patch is already applied."
		return 0
	fi

	if ! check_patch; then
		printf '%s\n' "$diagnostic" >&2
		echo "ERROR: $zephyr_dir/$source_file conflicts with the Zephyr $patch_name patch." >&2
		echo "Inspect it with: git -C \"$zephyr_dir\" diff -- $source_file" >&2
		exit 1
	fi
	if [[ "$mode" == check ]]; then
		echo "ERROR: Zephyr $patch_name patch is not applied." >&2
		echo 'Apply it from the workspace root: ./k-fsw/tools/zephyr-patch.sh' >&2
		exit 1
	fi

	if ! git -C "$zephyr_dir" apply "$patch_file"; then
		echo "ERROR: git could not write the Zephyr $patch_name patch into $zephyr_dir" >&2
		exit 1
	fi
	echo "Zephyr $patch_name patch applied."
}

for patch_name in 'shell Tab help' 'ZMS close error'; do
	if [[ "$patch_name" == 'shell Tab help' ]]; then
		source_file=subsys/shell/shell.c
		patch_file="$repo_dir/patches/zephyr/shell-tab-help.patch"
	else
		source_file=subsys/kvss/zms/zms.c
		patch_file="$repo_dir/patches/zephyr/zms-close-error.patch"
	fi
	if [[ ! -f "$zephyr_dir/$source_file" || ! -r "$zephyr_dir/$source_file" ]]; then
		echo "ERROR: Zephyr ${patch_name%% *} source not found or unreadable: $zephyr_dir/$source_file" >&2
		exit 1
	fi
	if [[ ! -f "$patch_file" || ! -r "$patch_file" ]]; then
		echo "ERROR: Cannot read $patch_file" >&2
		exit 1
	fi
	if [[ "$patch_name" == 'ZMS close error' ]]; then
		if [[ ! -r "$zephyr_dir/VERSION" ]] ||
		   [[ "$(sha256sum "$zephyr_dir/VERSION" | cut -d ' ' -f 1)" != \
		      fd6ba7636a003973a983412c2906173dc45ce3ba62386e407b212f9a5736ce01 ]]; then
			echo 'ERROR: ZMS close error patch requires pinned Zephyr v4.4.0.' >&2
			exit 1
		fi
		# Exact v4.4.0 preimage or the approved correction; no rebasing.
		read -r digest _ < <(sha256sum "$zephyr_dir/$source_file")
		case "$digest" in
			c724ccbe51ee412d72f098cdf6a44f97f9224df0454c36a7d9c94919cadd9c0d|20a253c5eee0f7f48bc6edbb152d079e13caf2dc92416d6e7a7e49462e47da97) ;;
			*)
				echo "ERROR: $zephyr_dir/$source_file conflicts with the pinned Zephyr ZMS close error patch." >&2
				exit 1 ;;
		esac
	fi
	apply_patch
done
