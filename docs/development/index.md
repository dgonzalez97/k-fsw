# Contributing {#development}

[TOC]

## Start a change

Keep one responsibility per branch. Include the tests and docs needed to
review that change. Preserve existing work before updating any repository.

From the workspace root:

```bash
git -C k-fsw status -sb
git -C k-fsw switch develop
git -C k-fsw pull --ff-only
. .venv/bin/activate
west manifest --validate
west update
```

Dependencies normally end up detached at their manifest SHA. Create a branch
before committing changes in one.

## Branches and commits

Push changes to `develop`, directly or through a feature PR. Update `main`
only by merging reviewed release changes; do not push commits directly to it.
Tag the release merge only after its CI/CD checks pass. Fix failed checks
through `develop` and merge the correction before releasing.
Keep unreleased work on `develop`; do not move published tags.

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

## Forked dependencies

`libcsp` and `libparam` are pinned to K-FSW forks, `dgonzalez97/kfsw-libcsp`
and `dgonzalez97/kfsw-libparam`. Check `west.yml` for the exact revisions and
each fork's `KFSW.md` for its changes from upstream.

Carry a change as one commit with its reason in the message, rebase the branch
onto upstream instead of merging upstream into it, and move the `west.yml`
revision the same way as any other dependency. Before tagging a release, check
whether upstream has new commits or tags since the pin.

A merge commit preserves the pinned SHA. Squash or rebase produces a new
SHA; update the pin and rerun composition CI if either is used. Do not
rewrite a commit already pinned by another PR.

## Choose the repository

| Change | Repository |
| --- | --- |
| Time, reset, watchdog, storage mechanism | `kfsw-platform` |
| Reusable service behaviour | `kfsw-services` |
| CSP, routing, packet buffers, transports | `kfsw-comms` |
| Device or subsystem client | `kfsw-modules` |
| Startup, targets, shell adapters, tools, integration tests, docs | `k-fsw` |

Public APIs go in that repository's `include/kfsw/` headers and private
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

Edit guides under `docs/` and document public C APIs in their headers.
Use short descriptions of what the code does. For procedures, give the command,
expected result and relevant limits. Link to upstream references for RTOS and
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

## Bench gate before a tag

Software CI says the code builds and behaves in simulation. It cannot say the
board keeps a log across a reset, answers over CAN, or comes back from an
interrupted update. A tag claims all of it, so the bench runs first.

Run it in this order. Each step is cheap compared to the one below it, and a
failure early usually explains the ones after.

| Step | Command | Needs |
| --- | --- | --- |
| 1 | `tools/ci/all.sh` | nothing; this is what CI runs |
| 2 | `tests/hil/preflight.sh` | nothing; says what the bench can serve |
| 3 | `tests/hil/run.sh board` | the board on its ST-LINK |
| 4 | `tests/hil/run.sh board-uart` | a serial bridge on the CSP UART |
| 5 | `tests/hil/run.sh board-can` | a CAN transceiver and a host adapter |
| 6 | `tests/hil/run.sh radio` | the radio pair |
| 7 | firmware update over CAN, below | a backup, and MCUboot on the board |

Record which steps ran, which were skipped for want of hardware, and the exact
output of each. A step nobody ran is not a step that passed, and the tag notes
say which is which.

### Preconditions that nothing checks for you

Every one of these has cost a bench session at least once.

- **Export the devices.** `run.sh <shape>` does it from `preflight.sh`. Calling a
  fixture directly does not, and the numbered `/dev/ttyACM*` you get by default
  is whichever board enumerated first.
- **Flash the image the fixture expects.** `can.robot` runs the CAN fixture with
  `--no-build`, so it tests whatever is already on the board. Build and flash it
  first, or run the fixture without Robot so it builds.
- **Clear the CAN error counters.** They saturate and stay saturated. The CAN
  fixture refuses to start unless the interface is `ERROR-ACTIVE`, and one of its
  assertions wants the counters at zero, so a bus that misbehaved earlier fails a
  run that is otherwise clean. Bring it up again:
  `sudo tests/hil/stm32/nucleo-l496zg/can-up.sh 500000 normal`
- **One ground profile per build directory.** `tools/k-ground` keys its build
  directory on the node number, and `kfsw-gnd-can`, `kfsw-gnd-uhf` and
  `kfsw-gnd-uhf-bench` are all node 16. Building one leaves its configuration
  where the next expects its own. Remove `build/k-ground/node-16` when switching
  between CAN and radio work.
- **A serial bridge needs its driver.** On a kernel that builds `ftdi_sio` as a
  module it has to be loaded, and a module whose BTF does not validate has to have
  that section stripped before it will load.

### Firmware update over CAN

This one changes the board: it installs MCUboot and moves the application into a
signed slot. Back the board up first, and keep the backup until the tag is out.

```bash
OCD=$ZEPHYR_SDK_INSTALL_DIR/hosttools/sysroots/x86_64-pokysdk-linux/usr/bin/openocd
S=$ZEPHYR_SDK_INSTALL_DIR/hosttools/sysroots/x86_64-pokysdk-linux/usr/share/openocd/scripts
$OCD -s $S -f ../zephyr/boards/st/nucleo_l496zg/support/openocd.cfg \
  -c init -c "reset halt" \
  -c "dump_image board-backup.bin 0x08000000 0x100000" \
  -c "reset run" -c shutdown
sha256sum board-backup.bin > board-backup.sha256
```

That is the whole 1 MB, so it covers the application, the golden region at
`0x080c0000` and LittleFS at `0x080f0000` without having to reason about which
matters. Restore it the same way with `flash write_image erase`.

Program only the bootloader and the image slots. Do not pass `--erase` to
`west flash`: the openocd runner has `stm32l4x mass_erase 0` configured as its
erase command, which takes the golden region and LittleFS with it.

Then follow `tests/hil/fwu/README.md`, which builds the ground node and the two
signed images, installs the baseline, and runs the acceptance.

The acceptance is not repeatable on its own: it leaves the board running the
candidate, and a second run refuses because the candidate and running revisions
must differ. Reinstall and confirm the baseline between runs.

Robot runs the same acceptance when both images are named:

```bash
export KFSW_FWU_CAN_GROUND=$B/ground/zephyr/zephyr.exe
export KFSW_FWU_CAN_IMAGE=$B/after/app/zephyr/zephyr.signed.bin
tests/hil/run.sh board-can
```

Without them that case is skipped, and a skip is not a pass.

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
