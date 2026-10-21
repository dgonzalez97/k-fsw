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

mkdir -p "$work_dir/zephyr/subsys/kvss/zms" "$work_dir/zephyr/subsys/shell" "$work_dir/.west" \
	"$work_dir/k-fsw/tools/ci" "$work_dir/k-fsw/patches/zephyr"
cp "$repo_dir/tools/zephyr-patch.sh" "$work_dir/k-fsw/tools/"
cp "$repo_dir/tools/ci/all.sh" "$work_dir/k-fsw/tools/ci/"
cp "$repo_dir/patches/zephyr/"*.patch "$work_dir/k-fsw/patches/zephyr/"
if [[ $# == 1 ]]; then
	cp "$1" "$work_dir/zephyr/subsys/shell/shell.c"
elif ! git -C "$repo_dir/../zephyr" show v4.4.0:subsys/shell/shell.c \
	> "$work_dir/zephyr/subsys/shell/shell.c"; then
	echo 'ERROR: cannot read shell.c at the pinned Zephyr revision; pass a pristine shell.c' >&2
	exit 1
fi
cp "$work_dir/zephyr/subsys/shell/shell.c" "$work_dir/pristine.c"
git -C "$repo_dir/../zephyr" show v4.4.0:subsys/kvss/zms/zms.c > "$work_dir/zephyr/subsys/kvss/zms/zms.c"
git -C "$repo_dir/../zephyr" show v4.4.0:VERSION > "$work_dir/zephyr/VERSION"
cp "$work_dir/zephyr/subsys/kvss/zms/zms.c" "$work_dir/pristine-zms.c"
mkdir -p "$work_dir/expected/subsys/kvss/zms"
cp "$work_dir/pristine-zms.c" "$work_dir/expected/subsys/kvss/zms/zms.c"
patch --batch --fuzz=0 -p1 -d "$work_dir/expected" < "$repo_dir/patches/zephyr/zms-close-error.patch"

mkdir -p "$work_dir/expected/subsys/shell"
cp "$work_dir/pristine.c" "$work_dir/expected/subsys/shell/shell.c"
patch --batch --fuzz=0 -p1 -d "$work_dir/expected" < "$repo_dir/patches/zephyr/shell-tab-help.patch"
if cmp -s "$work_dir/pristine.c" "$work_dir/expected/subsys/shell/shell.c"; then
	echo 'PATCH RESULT: FAIL (expected fixture is unchanged)'
	exit 1
fi
grep -F 'tab_item_print(sh, match->syntax, match->help, longest);' \
	"$work_dir/expected/subsys/shell/shell.c" >/dev/null

release_python="${KFSW_TEST_PYTHON:-$repo_dir/../.venv/bin/python3}"
[[ -x "$release_python" ]] || release_python=python3
"$release_python" - "$repo_dir" "$work_dir" <<'PYTHON'
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
from types import SimpleNamespace

repo, work = map(Path, sys.argv[1:])
spec = importlib.util.spec_from_file_location("release", repo / "tools/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
release.REPO = work / "k-fsw"
release.WORKSPACE = work
revision = "a" * 40
os.environ["KFSW_RELEASE_SOURCE"] = revision
project = SimpleNamespace(name="zephyr", path="zephyr", url="fixture", abspath=work / "zephyr")
release.projects = lambda: [project]
frozen = {"manifest": {"projects": [{"name": "zephyr", "path": "zephyr", "url": "fixture", "revision": revision}]}}
original_run = release.run
changed = ""

def fixture_run(*args, cwd=work):
    if args[0] == "git":
        if args[1] == "rev-parse":
            return revision
        if args[1] == "diff":
            return changed if cwd == project.abspath else ""
        if args[1] == "ls-files":
            return ""
        if args[1] == "apply":
            return original_run(*args, cwd=cwd)
        raise AssertionError(args)
    return original_run(*args, cwd=cwd)

release.run = fixture_run
original_check_output = subprocess.check_output
head_files = {"HEAD:subsys/shell/shell.c": (work / "pristine.c").read_bytes(),
              "HEAD:subsys/kvss/zms/zms.c": (work / "pristine-zms.c").read_bytes()}

def fixture_check_output(args, **kwargs):
    if args[:2] == ["git", "show"]:
        return head_files[args[2]]
    return original_check_output(args, **kwargs)

subprocess.check_output = fixture_check_output
try:
    release.source_state(frozen)
except subprocess.CalledProcessError:
    pass
else:
    raise AssertionError("release accepted clean unpatched Zephyr")
shutil.copy(work / "expected/subsys/shell/shell.c", work / "zephyr/subsys/shell/shell.c")
shutil.copy(work / "expected/subsys/kvss/zms/zms.c", work / "zephyr/subsys/kvss/zms/zms.c")
changed = "subsys/shell/shell.c\nsubsys/kvss/zms/zms.c"
assert release.source_state(frozen) == {"k-fsw": revision, "zephyr": revision}
# This workspace already carries the shell patch in its private baseline.
head_files["HEAD:subsys/shell/shell.c"] = (work / "expected/subsys/shell/shell.c").read_bytes()
assert release.source_state(frozen) == {"k-fsw": revision, "zephyr": revision}

changed += "\nunrelated.txt"
try:
    release.source_state(frozen)
except ValueError as error:
    assert "source tree is dirty: unrelated.txt" in str(error), error
else:
    raise AssertionError("release accepted unrelated dirt")
shell = work / "zephyr/subsys/shell/shell.c"
shell.write_text(shell.read_text() + "\n/* unrelated edit */\n")
changed = "subsys/shell/shell.c\nsubsys/kvss/zms/zms.c"
try:
    release.source_state(frozen)
except ValueError as error:
    assert "source differs from approved patches" in str(error), error
else:
    raise AssertionError("release accepted unrelated same-file dirt")
shutil.copy(work / "pristine.c", shell)
shutil.copy(work / "pristine-zms.c", work / "zephyr/subsys/kvss/zms/zms.c")
print("RELEASE PATCH RESULT: PASS")
PYTHON

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
cmp "$work_dir/expected/subsys/kvss/zms/zms.c" "$work_dir/zephyr/subsys/kvss/zms/zms.c"
cp "$work_dir/zephyr/subsys/kvss/zms/zms.c" "$work_dir/after-first-zms.c"

"$work_dir/k-fsw/tools/zephyr-patch.sh" > "$work_dir/repeat.log"
grep -F 'already applied' "$work_dir/repeat.log"
cmp "$work_dir/after-first.c" "$work_dir/zephyr/subsys/shell/shell.c"
"$work_dir/k-fsw/tools/zephyr-patch.sh" --check
cmp "$work_dir/after-first.c" "$work_dir/zephyr/subsys/shell/shell.c"

cmp "$work_dir/after-first-zms.c" "$work_dir/zephyr/subsys/kvss/zms/zms.c"
cp "$work_dir/pristine-zms.c" "$work_dir/zephyr/subsys/kvss/zms/zms.c"
if "$work_dir/k-fsw/tools/zephyr-patch.sh" --check > "$work_dir/zms-missing.log" 2>&1; then
	echo 'PATCH RESULT: FAIL (missing ZMS correction accepted)'
	exit 1
fi
grep -F 'ZMS close error patch is not applied' "$work_dir/zms-missing.log"
cmp "$work_dir/pristine-zms.c" "$work_dir/zephyr/subsys/kvss/zms/zms.c"
"$work_dir/k-fsw/tools/zephyr-patch.sh"
printf '\n/* unexpected source */\n' >> "$work_dir/zephyr/subsys/kvss/zms/zms.c"
cp "$work_dir/zephyr/subsys/kvss/zms/zms.c" "$work_dir/conflict-zms.c"
if "$work_dir/k-fsw/tools/zephyr-patch.sh" > "$work_dir/zms-conflict.log" 2>&1; then
	echo 'PATCH RESULT: FAIL (unexpected ZMS source accepted)'
	exit 1
fi
grep -F 'conflicts with the pinned Zephyr ZMS close error patch' "$work_dir/zms-conflict.log"
cmp "$work_dir/conflict-zms.c" "$work_dir/zephyr/subsys/kvss/zms/zms.c"
cp "$work_dir/after-first-zms.c" "$work_dir/zephyr/subsys/kvss/zms/zms.c"

cp "$work_dir/zephyr/VERSION" "$work_dir/pinned-version"
printf '%s\n' 'VERSION_MAJOR = 5' > "$work_dir/zephyr/VERSION"
if "$work_dir/k-fsw/tools/zephyr-patch.sh" --check > "$work_dir/version.log" 2>&1; then
	echo 'PATCH RESULT: FAIL (unexpected Zephyr version accepted)'
	exit 1
fi
grep -F 'requires pinned Zephyr v4.4.0' "$work_dir/version.log"
cmp "$work_dir/after-first-zms.c" "$work_dir/zephyr/subsys/kvss/zms/zms.c"
cp "$work_dir/pinned-version" "$work_dir/zephyr/VERSION"

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
