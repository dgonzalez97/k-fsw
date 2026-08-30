# Development Workflow {#development}

## One change, one reviewed story

K-FSW development starts with a concrete issue and ends when the integrated
composition is reviewed and green. The ordinary flow is:

```text
issue
  |
topic branch
  |
focused commits
  |
push branch
  |
pull request
  |
hosted CI
  |
review
  |
merge commit
  |
main
```

Keep behavior, its tests, and directly coupled documentation in the same
reviewed story. Do not work directly on `main`, reuse an unrelated branch, or
leave a public guide describing behavior that only exists in an unpublished
dependency checkout.

## Start from a clean integrated baseline

In the composition repository:

```bash
cd /path/to/k-fsw-workspace/k-fsw
git status -sb
git switch main
git pull --ff-only
```

Then update the workspace from the merged manifest:

```bash
cd ..
. .venv/bin/activate
west manifest --validate
west update
```

Inspect dependency status after an update. Detached `HEAD` at the manifest SHA
is normal. Modified tracked files or untracked work are not; determine their
owner before proceeding.

## Branch naming

Use the issue number and a short lowercase slug.

| Change | Pattern | Example |
| --- | --- | --- |
| Feature | `feature/<issue>-<slug>` | `feature/42-command-router` |
| Refactor | `refactor/<issue>-<slug>` | `refactor/23-param-csp` |
| Fix | `fix/<issue>-<slug>` | `fix/43-storage-timeout` |
| Tests | `test/<issue>-<slug>` | `test/44-param-errors` |
| CI | `ci/<issue>-<slug>` | `ci/15-docs-pages` |
| Documentation | `docs/<issue>-<slug>` | `docs/26-engineering-guide` |

Create the issue first, then branch from the repository revision that should
receive the change:

```bash
git switch -c docs/26-engineering-guide
```

For a dependency repository currently detached at its west pin, this command
also gives new work a branch before the first commit.

## Commit identity and subjects

Use the project Git identity:

```bash
git config user.name dgonzalez97
git config user.email 103115570+dgonzalez97@users.noreply.github.com
```

Commit subjects identify the area and intent:

```text
[PLATFORM][STORAGE] Reject non-erased corrupt media
[SERVICES][PARAM] Separate CSP adapter from local table
[TEST][HIL] Add multi-board shell smoke
[DOCS][COMMS] Explain CSP, KISS, and RDP data flow
```

Prefer a small number of coherent commits. A commit should be reviewable and
leave its repository internally consistent. There is no benefit in splitting
one paragraph per commit or combining unrelated production and documentation
changes merely to reduce commit count.

## Review a local change

Before committing:

```bash
git status -sb
git diff --check
git diff
```

Run the narrow tests while iterating and the appropriate combined gates before
review. A documentation-only change normally needs Doxygen, PDF, local-link
checks, and visual inspection rather than rebuilding unrelated MCU services.
An API or composition change needs the software checks it can affect.

Do not use `west update` or a pristine build to hide a dirty dependency. Git
status in all owned repositories should be part of a multi-repository review.

## Pull requests

Push the topic branch and create a PR that closes the issue:

```bash
git push -u origin docs/26-engineering-guide
gh pr create \
  --base main \
  --head docs/26-engineering-guide \
  --title "[DOCS][GUIDE] Expand K-FSW engineering guide and project status" \
  --body "Closes #26"
```

Wait for every required Software CI job. Pull requests build Doxygen but do not
deploy Pages. After approval, merge with a merge commit and remove the remote
topic branch when it is no longer needed.

Repository protection is GitHub administrative state, not source. The intended
policy is PR review, required green checks, no force-push to `main`, and direct
push only for explicit recovery. Confirm actual repository settings instead of
assuming the manual enforces them.

## west-managed dependencies

### Why dependencies are detached

`west.yml` pins exact commits. `west update` uses a local `manifest-rev` and
checks out the selected commit, not a floating dependency branch:

```text
k-fsw/west.yml
  kfsw-services revision: 32260f8...
                 |
             west update
                 |
kfsw-services HEAD detached at 32260f8...
```

Detached state is valuable for reproducibility: every developer and CI job
builds the same commit. It means a developer must create/switch a branch before
committing dependency work.

### Work in one dependency

Suppose a service change is required:

```bash
cd /path/to/k-fsw-workspace/kfsw-services
git status -sb
git switch -c feature/30-new-service
```

Implement, test, and commit it in `kfsw-services`. The composition repository
should contain only integration/configuration/tests/docs that it owns, plus the
manifest revision update.

### The two-PR pattern

A dependency commit must be available from its declared GitHub remote before
hosted composition CI can resolve it.

```text
dependency issue/branch
        |
dependency commits + tests
        |
push dependency branch
        |
dependency PR --------------------------+
        |                               |
published exact commit                  |
        |                               |
update k-fsw west.yml to that SHA       |
        |                               |
k-fsw integration/tests/docs            |
        |                               |
k-fsw composition PR                    |
        |                               |
hosted CI resolves published pin        |
        |                               |
merge dependency PR first <-------------+
        |
merge composition PR
```

Concrete sequence:

1. Create the dependency branch from the current manifest pin.
2. Commit and run repository/module tests.
3. Push the dependency branch and open its PR.
4. Copy the exact published dependency commit into `k-fsw/west.yml`.
5. Commit K-FSW lifecycle/configuration/integration/documentation changes on a
   separate K-FSW branch.
6. Open the composition PR and let hosted CI build from scratch.
7. Merge the dependency PR first, preserving the pinned commit.
8. Merge the composition PR only while the SHA is still reachable from the
   declared remote.

Get the exact pin with:

```bash
git -C ../kfsw-services rev-parse HEAD
```

Then edit the matching `revision` in `west.yml` and validate:

```bash
west manifest --validate
west manifest --resolve
```

### Why merge strategy matters

If `west.yml` pins the dependency branch tip, a merge commit preserves that
tip as an ancestor reachable from `main`. A squash or rebase creates different
commits. If repository policy requires either, update the manifest to the final
reachable SHA and rerun composition CI before merging.

Never force-push or rewrite a dependency commit already pinned by an open or
merged composition PR. Exact pins are cross-repository interfaces.

### Testing candidate pins locally

A K-FSW composition branch may point at a published dependency feature commit
before its PR merges. Run:

```bash
west update kfsw-services
git -C kfsw-services status -sb
git -C kfsw-services rev-parse HEAD
```

Confirm the resulting SHA exactly matches `west.yml`. Do not switch the
dependency back to some convenient local branch and treat that as a successful
composition test.

## Choosing the owning repository

Ask what contract is changing:

| Change | Normal owner |
| --- | --- |
| Cross-target monotonic/reset/storage lifecycle | `kfsw-platform` |
| Logging, parameter, persistence, or FTP service behavior | `kfsw-services` |
| CSP initialization, route/interface, KISS, packet ownership | `kfsw-comms` |
| Reusable mission-specific device or subsystem behavior | `kfsw-modules` |
| Target mapping/defaults, devicetree binding, application order, shell adapter, integration/HIL, aggregate docs | `k-fsw` |

A public header belongs under the owner's `include/kfsw/` tree only when other
modules are meant to consume it. Private protocol/layout helpers stay in the
owner's `src/` tree. Avoid creating a cross-repository API solely to move one
internal function to another directory.

## Kconfig and devicetree changes

Use Kconfig for software selection and dependency validation. Use devicetree
for hardware instances, wiring, flash regions, and driver properties. Product
defaults and lifecycle selection live in `k-fsw`.

For a new optional service:

1. define a Kconfig symbol and honest prerequisites in its owning module;
2. conditionally compile its sources;
3. expose only the needed public lifecycle/operation API;
4. select it in the appropriate K-FSW target configuration;
5. add startup and shell integration in K-FSW; and
6. test both enabled behavior and meaningful disabled compositions.

For a new device, prefer a devicetree chosen property or binding over a board
name in generic C. Confirm the final generated devicetree and `.config`, not
only the source overlay.

## VS Code workspace

Open the committed multi-root workspace:

```bash
code k-fsw/K-FSW.code-workspace
```

It exposes the five owned source repositories plus one `Active Build` folder:

```text
K-FSW.code-workspace
├── k-fsw
├── kfsw-platform
├── kfsw-services
├── kfsw-comms
├── kfsw-modules
└── Active Build -> build/<selected-target>
```

The raw west workspace is not the recommended VS Code root. It contains the
entire Zephyr source tree, imported modules, upstream third-party code, Python
environment, and every build output. Letting Microsoft C/C++ TagParser browse
that tree globally creates an expensive, noisy symbol database unrelated to
most K-FSW edits.

The committed workspace restricts global browse/search/watch roots to
project-owned source. Upstream headers remain navigable when included because
the selected `compile_commands.json` provides exact include paths, generated
headers, defines, compiler, and flags for each translation unit.

## Select IntelliSense build state

Build a supported IntelliSense target and select it:

```bash
./k-fsw/tools/build.sh linux
./k-fsw/tools/select-intellisense.sh linux
```

Or select MCU-specific generated state:

```bash
./k-fsw/tools/build.sh nucleo_l496zg
./k-fsw/tools/select-intellisense.sh nucleo_l496zg
```

The selector creates/updates symlinks:

```text
build/compile_commands.json -> <target>/compile_commands.json
build/active                -> <target>
build/<target>/.vscode/settings.json -> exclusion template
```

It refuses to overwrite non-symlink paths. It does not copy or generate
compilation data; rebuilding the target refreshes the real database. After
changing target, refresh Explorer and run `C/C++: Reset IntelliSense Database`
if the extension retains stale definitions.

Only Linux and NUCLEO are accepted by the selector because they are the full
reference development configurations. FRDM/Pico shell profiles can still be
built and inspected manually.

## Debugging generated and target-specific behavior

Start with the compilation database, `.config`, and devicetree output for the
actual target. A line compiled for Linux may be absent on NUCLEO, and a device
selected by one overlay may not exist on another.

For NUCLEO source debugging:

```bash
./k-fsw/tools/debugserver.sh nucleo_l496zg
./k-fsw/tools/debug.sh nucleo_l496zg
```

The ELF is `build/nucleo_l496zg/zephyr/zephyr.elf`. Use the selected build's
symbols; pointing GDB at a stale or Linux ELF can make source breakpoints look
plausible while addresses are wrong.

Logs and shell status are part of diagnosis. `@READY` alone does not prove a
service started. Check the earlier error lines and service-specific `info`
command before stepping into a lower layer.

## Documentation work

Aggregate concepts, target instructions, current status, and multi-repository
workflow belong under `k-fsw/docs/`. Exact public declarations belong as
Doxygen comments in the owning repository headers. Ownership-specific README
files may summarize how that repository integrates, but should not duplicate
the whole manual.

For a manual change:

```bash
./k-fsw/tools/docs/build.sh
./k-fsw/tools/docs/pdf.sh
git -C k-fsw diff --check
```

Inspect HTML navigation and the PDF. Keep generated HTML/PDF out of Git. The
printable manual contains concepts and workflows; the generated API indexes
remain HTML-only.

When status changes, verify the claim against source, Kconfig, target defaults,
tests, CI, physical evidence, and issues. Avoid using an old roadmap as proof
that software exists.

## Before requesting review

Check:

- the issue and branch describe the same scope;
- Git identity is correct;
- every dependency pin is exact, published, and reachable;
- all owned repositories are in an understood state;
- generated configuration matches the intended target;
- relevant software gates pass;
- required physical evidence is attached or clearly marked manual/pending;
- documentation distinguishes implementation, software testing, and physical
  verification; and
- `git diff --check` is clean.

Then push, open the PR, and let the hosted workflow reproduce the composition
from scratch.
