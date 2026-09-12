# Contributing {#development}

[TOC]

## Start a change

Keep one responsibility per branch. Include the tests and docs needed to
review that change. Preserve existing work before updating any repository.

From the workspace root:

```bash
git -C k-fsw status -sb
git -C k-fsw switch main
git -C k-fsw pull --ff-only
. .venv/bin/activate
west manifest --validate
west update
```

Dependencies normally end up detached at their manifest SHA. Create a branch
before committing changes in one.

## Branches and commits

Use `<type>/<issue>-<slug>`, or `<type>/<slug>` when no issue exists.

| Type | Example |
| --- | --- |
| Feature | `feature/42-command-router` |
| Fix | `fix/43-storage-timeout` |
| Refactor | `refactor/23-param-csp` |
| Documentation | `docs/wiki-layout` |

Check your identity before committing:

```bash
git config user.name
git config user.email
```

Use a short imperative subject with uppercase area tags:

```text
[SERVICES][PARAM] Reject invalid table offsets
[TEST][HIL] Add CAN parameter checks
[DOCS][GUIDE] Simplify wiki navigation
```

One commit per responsibility. Add a body only when the reason is not clear
from the subject.

## Pull requests

Use the same title format as commits. Keep the body to the change, relevant
checks, and issue number if there is one:

```text
Group the docs by task and use the demo colours.
Checked: Doxygen, PDF, links, desktop and mobile layout.
```

Before opening the PR:

```bash
git status --short
git diff --stat
git diff --check
```

Run checks appropriate to the change; @ref testing lists the entry points.
Docs changes need Doxygen, PDF, link checks, and visual inspection.
Record physical tests only when they were observed on the bench.

## Change a west dependency

Each reusable repository has its own branch and PR. The composition PR pins
the dependency commit and contains the integration changes.

1. Branch from the dependency's current manifest pin.
2. Implement, test, and commit there.
3. Publish the dependency branch and open its PR.
4. Copy its exact SHA into `k-fsw/west.yml`.
5. Test the composition and open the `k-fsw` PR.
6. Merge the dependency first, then the composition.

For example, inside `kfsw-services`:

```bash
git status -sb
git switch -c feature/30-new-service
```

After publishing the dependency commit, from the workspace root:

```bash
git -C kfsw-services rev-parse HEAD
# Put this SHA in k-fsw/west.yml.
west manifest --validate
west update kfsw-services
git -C kfsw-services rev-parse HEAD
```

The checked-out SHA must match the manifest. Hosted CI must be able to fetch
it from the declared remote.

A merge commit preserves the pinned SHA. Squash or rebase produces a new
SHA; update the pin and rerun composition CI if either is used. Do not
rewrite a commit already pinned by another PR.

## Choose the repository

| Change | Owner |
| --- | --- |
| Time, reset, watchdog, storage mechanism | `kfsw-platform` |
| Reusable service behaviour | `kfsw-services` |
| CSP, routing, packet ownership, transports | `kfsw-comms` |
| Device or subsystem client | `kfsw-modules` |
| Startup, targets, shell adapters, tools, integration tests, docs | `k-fsw` |

Public APIs belong in the owner's `include/kfsw/` headers. Keep private
helpers in its source tree.

For a new service, add its Kconfig dependencies, conditional build, public
API, application startup, and tests for enabled and disabled configurations.
Use devicetree for devices and wiring. See @ref architecture.

## VS Code and debugging

Open the multi-repository workspace:

```bash
code k-fsw/K-FSW.code-workspace
```

It includes the five project repositories and `build/active`. Select the
compilation database for the target you are editing:

```bash
./k-fsw/tools/build.sh linux
./k-fsw/tools/select-intellisense.sh linux
```

Use `nucleo_l496zg` for MCU configuration. The selector updates symlinks to
the selected build; it refuses to overwrite regular files. Rebuild to
refresh generated headers and `compile_commands.json`.

For NUCLEO debugging, run these in separate terminals:

```bash
./k-fsw/tools/debugserver.sh nucleo_l496zg
./k-fsw/tools/debug.sh nucleo_l496zg
```

Use `build/nucleo_l496zg/zephyr/zephyr.elf` and check that build's
`.config` and `zephyr.dts` when diagnosing target-specific behaviour.

## Documentation

Edit guides under `docs/` and document public C APIs in their owning
headers. Keep instructions short: command, expected result, then limits
that affect its use. Link to upstream references for general RTOS and
protocol background.

From the workspace root:

```bash
./k-fsw/tools/docs/build.sh
./k-fsw/tools/docs/pdf.sh
git -C k-fsw diff --check
```

Check navigation, images, tables, and links in HTML and PDF. Keep generated
output out of Git. Update @ref project_status from source and recorded
test results when capabilities change.

## Release builds

`tools/release.py` checks the source and signing inputs, builds twice, and
compares the unsigned application payloads. It verifies both signatures and
the bootloader's trust key. It does not create a tag or publish artifacts.

Use the MCUboot profiles in @ref firmware_update, then set:

| Input | Value |
| --- | --- |
| `KFSW_IMAGE_VERSION` | MCUboot version, for example `1.0.0+0` |
| `KFSW_RELEASE_SOURCE` | Full `k-fsw` commit SHA |
| `KFSW_RELEASE_MANIFEST` | Frozen manifest from `tools/release.py freeze` |
| `SOURCE_DATE_EPOCH` | Fixed source timestamp, in Unix seconds |
| `ZEPHYR_SDK_INSTALL_DIR` | SDK directory |
| `KFSW_RELEASE_COMPILER_SHA256` | SHA256 of the SDK's `arm-zephyr-eabi-gcc` |
| `KFSW_MCUBOOT_KEY` | Private ECDSA P-256 signing key; development keys are rejected |

From `k-fsw`, with the workspace virtual environment active:

```bash
python tools/release.py check
python tools/release.py build --output ../build/release-1.0.0
```

The output directory must be new. Keep `artifacts/release.json` with the images;
it records sources, tool versions, public-key fingerprint, and artifact hashes.
