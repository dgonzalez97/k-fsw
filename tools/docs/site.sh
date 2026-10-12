#!/usr/bin/env bash
set -Eeuo pipefail

# Build the manual for several versions into one directory tree, so the site
# can offer a version selector. GitHub Pages replaces the whole site on every
# deployment, so every version a reader can reach has to be built in one run.

KFSW_SITE_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_DOCS_TOOLS_DIR="$(dirname "$KFSW_SITE_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_DOCS_TOOLS_DIR")")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
KFSW_SITE_OUTPUT="${KFSW_OUTPUT_ROOT:-$KFSW_WORKSPACE_ROOT/build}/docs/site"

# The repositories the Doxyfile reads. The manual needs no toolchain and no
# Zephyr, so a version is built from git worktrees and never from the network.
readonly DEPENDENCIES=(kfsw-platform kfsw-services kfsw-comms kfsw-modules)

staging="$(mktemp -d /tmp/kfsw-docs-site.XXXXXX)"
worktrees=()

cleanup()
{
	for worktree in "${worktrees[@]:-}"; do
		[[ -n "$worktree" ]] || continue
		git -C "${worktree%%:*}" worktree remove --force "${worktree#*:}" 2>/dev/null || true
	done
	rm -rf -- "$staging"
}
trap cleanup EXIT

# One entry per release line, newest tag in it, plus the two branches a reader
# follows. Every patch tag would be a wall of near-identical choices.
versions_to_build()
{
	local branch tag major
	for branch in main develop; do
		if git -C "$KFSW_REPO_DIR" rev-parse --verify --quiet "$branch" >/dev/null; then
			printf '%s\t%s\t%s\n' "$branch" "$branch" "$branch"
		fi
	done
	for major in $(git -C "$KFSW_REPO_DIR" tag --list 'v[0-9]*' |
		sed -n 's/^v\([0-9]\+\)\..*/\1/p' | sort -un); do
		tag="$(git -C "$KFSW_REPO_DIR" tag --list "v$major.*" --sort=-v:refname | head -1)"
		[[ -n "$tag" ]] && printf 'v%s\t%s\t%s\n' "$major" "$tag" "$tag"
	done
}

pinned_revision() # manifest, project
{
	python3 - "$1" "$2" <<'PYTHON'
import sys, yaml

manifest, project = sys.argv[1], sys.argv[2]
with open(manifest) as handle:
    document = yaml.safe_load(handle)
for entry in document["manifest"]["projects"]:
    if entry.get("name") == project:
        print(entry.get("revision", ""))
        break
PYTHON
}

add_worktree() # repository, ref, destination
{
	if ! git -C "$1" rev-parse --verify --quiet "$2^{commit}" >/dev/null; then
		# A CI checkout is narrow, so the revision a manifest pins may not
		# be here yet. Ask for it once before giving up on the version.
		git -C "$1" fetch --quiet --tags origin "$2" 2>/dev/null ||
			git -C "$1" fetch --quiet --tags origin 2>/dev/null || true
		if ! git -C "$1" rev-parse --verify --quiet "$2^{commit}" >/dev/null; then
			return 1
		fi
	fi
	git -C "$1" worktree add --detach --quiet "$3" "$2"
	worktrees+=("$1:$3")
}

build_version() # slug, ref
{
	local slug="$1" ref="$2" tree="$staging/$1" revision
	local source="$tree/k-fsw"

	add_worktree "$KFSW_REPO_DIR" "$ref" "$source" || {
		echo "SITE: skipping $slug, $ref is not in this clone"
		return 1
	}
	for project in "${DEPENDENCIES[@]}"; do
		revision="$(pinned_revision "$source/west.yml" "$project")"
		if [[ -z "$revision" ]]; then
			echo "SITE: skipping $slug, $project is not pinned in its manifest"
			return 1
		fi
		add_worktree "$KFSW_WORKSPACE_ROOT/$project" "$revision" "$tree/$project" || {
			echo "SITE: skipping $slug, $project $revision is not in this clone"
			return 1
		}
	done

	echo "SITE: building $slug from $ref"
	KFSW_OUTPUT_ROOT="$tree/out" "$source/tools/docs/build.sh" >"$tree/build.log" 2>&1 || {
		echo "SITE: $slug failed to build"
		sed -n '1,40p' "$tree/build.log"
		return 1
	}
	mkdir -p "$KFSW_SITE_OUTPUT/$slug"
	cp -r "$tree/out/docs/html/." "$KFSW_SITE_OUTPUT/$slug/"
	return 0
}

# A tag's own Doxyfile knows nothing about the selector, so the script is added
# to the pages after they are generated rather than through HTML_HEADER.
inject_selector() # slug
{
	local page
	while IFS= read -r -d '' page; do
		grep -q 'kfsw-versions.js' "$page" && continue
		sed -i 's#</body>#<script src="../kfsw-versions.js" defer></script>\n</body>#' "$page"
	done < <(find "$KFSW_SITE_OUTPUT/$1" -name '*.html' -print0)
}

rm -rf -- "$KFSW_SITE_OUTPUT"
mkdir -p "$KFSW_SITE_OUTPUT"

built=()
skipped=()
default=""
while IFS=$'\t' read -r slug label ref; do
	if build_version "$slug" "$ref"; then
		inject_selector "$slug"
		built+=("$slug:$label")
		[[ -z "$default" ]] && default="$slug"
	else
		skipped+=("$slug")
	fi
done < <(versions_to_build)

if [[ ${#built[@]} -eq 0 ]]; then
	echo "SITE RESULT: FAIL (no version built)"
	exit 1
fi

python3 - "$KFSW_SITE_OUTPUT" "$default" "${built[@]}" <<'PYTHON'
import json, sys

output, default, *entries = sys.argv[1:]
versions = [{"slug": entry.split(":", 1)[0], "label": entry.split(":", 1)[1]} for entry in entries]
with open(f"{output}/versions.json", "w") as handle:
    json.dump({"default": default, "versions": versions}, handle, indent=2)
    handle.write("\n")
with open(f"{output}/index.html", "w") as handle:
    handle.write(
        "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
        f"<meta http-equiv=\"refresh\" content=\"0; url={default}/index.html\">\n"
        "<title>K-FSW</title>\n</head>\n<body>\n"
        f"<p><a href=\"{default}/index.html\">K-FSW manual</a></p>\n</body>\n</html>\n"
    )
PYTHON

cp "$KFSW_REPO_DIR/docs/theme/kfsw-versions.js" "$KFSW_SITE_OUTPUT/kfsw-versions.js"

echo "SITE: versions: ${built[*]}"
# A reader cannot tell a version that was never offered from one that does not
# exist, so say which were left out rather than publishing a short list quietly.
if [[ ${#skipped[@]} -gt 0 ]]; then
	echo "SITE: not published: ${skipped[*]}"
fi
echo "SITE RESULT: PASS ($KFSW_SITE_OUTPUT/index.html)"
