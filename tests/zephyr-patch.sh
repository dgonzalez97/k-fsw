#!/usr/bin/env bash
set -Eeuo pipefail

repo_dir="$(dirname "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")")"
if [[ $# -gt 1 ]]; then
	echo "Usage: $0 [pristine-shell.c]" >&2
	exit 2
fi
if ! command -v patch >/dev/null 2>&1; then
	echo 'ERROR: patch is required' >&2
	exit 1
fi
work_dir="$(mktemp -d /tmp/kfsw-zephyr-patch.XXXXXX)"
trap 'rm -rf -- "$work_dir"' EXIT

mkdir -p "$work_dir/zephyr/subsys/shell" "$work_dir/.west" \
	"$work_dir/k-fsw/tools/ci" "$work_dir/k-fsw/patches/zephyr"
cp "$repo_dir/tools/zephyr-patch.sh" "$work_dir/k-fsw/tools/"
cp "$repo_dir/tools/ci/all.sh" "$work_dir/k-fsw/tools/ci/"
cp "$repo_dir/patches/zephyr/shell-tab-help.patch" "$work_dir/k-fsw/patches/zephyr/"
if [[ $# == 1 ]]; then
	cp "$1" "$work_dir/zephyr/subsys/shell/shell.c"
elif ! git -C "$repo_dir/../zephyr" show manifest-rev:subsys/shell/shell.c \
	> "$work_dir/zephyr/subsys/shell/shell.c"; then
	echo 'ERROR: cannot read shell.c at the pinned Zephyr revision; pass a pristine shell.c' >&2
	exit 1
fi
cp "$work_dir/zephyr/subsys/shell/shell.c" "$work_dir/pristine.c"

mkdir -p "$work_dir/expected/subsys/shell"
cp "$work_dir/pristine.c" "$work_dir/expected/subsys/shell/shell.c"
patch --batch --fuzz=0 -p1 -d "$work_dir/expected" < "$repo_dir/patches/zephyr/shell-tab-help.patch"
if cmp -s "$work_dir/pristine.c" "$work_dir/expected/subsys/shell/shell.c"; then
	echo 'PATCH RESULT: FAIL (expected fixture is unchanged)'
	exit 1
fi
grep -F 'tab_item_print(sh, match->syntax, match->help, longest);' \
	"$work_dir/expected/subsys/shell/shell.c" >/dev/null

if "$work_dir/k-fsw/tools/ci/all.sh" > "$work_dir/ci.log" 2>&1; then
	echo 'PATCH RESULT: FAIL (CI accepted an unpatched tree)'
	exit 1
fi
grep -F 'ERROR: Zephyr shell Tab help patch is not applied' "$work_dir/ci.log"
grep -F './k-fsw/tools/zephyr-patch.sh' "$work_dir/ci.log"
cmp "$work_dir/pristine.c" "$work_dir/zephyr/subsys/shell/shell.c"

"$work_dir/k-fsw/tools/zephyr-patch.sh"
cmp "$work_dir/expected/subsys/shell/shell.c" "$work_dir/zephyr/subsys/shell/shell.c"
cp "$work_dir/zephyr/subsys/shell/shell.c" "$work_dir/after-first.c"
"$work_dir/k-fsw/tools/zephyr-patch.sh" > "$work_dir/repeat.log"
grep -F 'already applied' "$work_dir/repeat.log"
cmp "$work_dir/after-first.c" "$work_dir/zephyr/subsys/shell/shell.c"
"$work_dir/k-fsw/tools/zephyr-patch.sh" --check
cmp "$work_dir/after-first.c" "$work_dir/zephyr/subsys/shell/shell.c"

printf '%s\n' 'conflicting shell source' > "$work_dir/zephyr/subsys/shell/shell.c"
cp "$work_dir/zephyr/subsys/shell/shell.c" "$work_dir/conflict.c"
if "$work_dir/k-fsw/tools/zephyr-patch.sh" > "$work_dir/conflict.log" 2>&1; then
	echo 'PATCH RESULT: FAIL (conflicting source accepted)'
	exit 1
fi
grep -F 'conflicts with the Zephyr shell Tab help patch.' "$work_dir/conflict.log"
cmp "$work_dir/conflict.c" "$work_dir/zephyr/subsys/shell/shell.c"

if "$work_dir/k-fsw/tools/zephyr-patch.sh" --check "$work_dir/absent" > "$work_dir/missing.log" 2>&1; then
	echo 'PATCH RESULT: FAIL (missing directory accepted)'
	exit 1
fi
grep -F 'Zephyr directory not found' "$work_dir/missing.log"
mv "$work_dir/zephyr/subsys/shell/shell.c" "$work_dir/conflict-source.c"
if "$work_dir/k-fsw/tools/zephyr-patch.sh" --check > "$work_dir/source.log" 2>&1; then
	echo 'PATCH RESULT: FAIL (missing source accepted)'
	exit 1
fi
grep -F 'Zephyr shell source not found' "$work_dir/source.log"
cp "$work_dir/pristine.c" "$work_dir/zephyr/subsys/shell/shell.c"
mv "$work_dir/k-fsw/patches/zephyr/shell-tab-help.patch" "$work_dir/saved.patch"
if "$work_dir/k-fsw/tools/zephyr-patch.sh" --check > "$work_dir/unreadable.log" 2>&1; then
	echo 'PATCH RESULT: FAIL (unreadable patch accepted)'
	exit 1
fi
grep -F 'Cannot read' "$work_dir/unreadable.log"
printf '%s\n' 'invalid patch' > "$work_dir/k-fsw/patches/zephyr/shell-tab-help.patch"
if "$work_dir/k-fsw/tools/zephyr-patch.sh" --check > "$work_dir/git.log" 2>&1; then
	echo 'PATCH RESULT: FAIL (invalid patch accepted)'
	exit 1
fi
grep -E 'error:|fatal:' "$work_dir/git.log"
grep -F 'git could not check' "$work_dir/git.log"
if grep -F 'conflicts' "$work_dir/git.log"; then
	echo 'PATCH RESULT: FAIL (git error reported as conflict)'
	exit 1
fi
cmp "$work_dir/pristine.c" "$work_dir/zephyr/subsys/shell/shell.c"

cp "$work_dir/saved.patch" "$work_dir/k-fsw/patches/zephyr/shell-tab-help.patch"
mkdir "$work_dir/bin"
cat > "$work_dir/bin/git" <<'SH'
#!/usr/bin/env bash
printf '%s\n' 'fatal: detected dubious ownership in repository' >&2
exit 128
SH
chmod +x "$work_dir/bin/git"
if PATH="$work_dir/bin:$PATH" \
	"$work_dir/k-fsw/tools/zephyr-patch.sh" --check > "$work_dir/owner.log" 2>&1; then
	echo 'PATCH RESULT: FAIL (ownership error accepted)'
	exit 1
fi
grep -F 'dubious ownership' "$work_dir/owner.log"
grep -F 'git could not check' "$work_dir/owner.log"
if grep -E 'conflicts|Apply it from' "$work_dir/owner.log"; then
	echo 'PATCH RESULT: FAIL (ownership error misreported)'
	exit 1
fi
cmp "$work_dir/pristine.c" "$work_dir/zephyr/subsys/shell/shell.c"

echo 'PATCH RESULT: PASS'
