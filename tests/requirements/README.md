# Required evidence

These selections map a small set of requirements to existing tests. A test name
is its exact suite path plus its case name; a bare case name is insufficient.
The manifest's `id` names the requirement and `source` points to its Robot file.
Both requirement IDs and selected test identities must be unique.
Selections may also declare `evidence_kind` as `hosted` or `physical`.

The optional [image binding check](image-binding.md) compares actual report,
image and resolved configuration hashes with an independently recorded expected
source/profile context. It checks consistency; the controlled runner must retain
the build and execution record that establishes the pair.

From the workspace root, check an executed software report:

```bash
python3 k-fsw/tools/ci/robot-counts.py build/robot/software/output.xml \
  --required k-fsw/tests/requirements/software.json
```

For the board stage's L496 Robot report:

```bash
python3 k-fsw/tools/ci/robot-counts.py build/robot/board/board/output.xml \
  --board --required k-fsw/tests/requirements/l496.json
```

An explicit `tests/hil/run.sh board` invocation defaults to `build/robot/output.xml`;
use its actual output path instead. The counter does not execute tests or flash.
Each selected test must appear exactly once with PASS. Missing, skipped, failed,
duplicate or malformed evidence fails. Unselected skips retain the normal
developer behavior; `KFSW_ROBOT_ACCEPTANCE=1` still requires zero software skips.
The Robot CI job checks the software selection after running its scenarios.
The fixtures run through `tests/ci-suite-test.py`, already part of quality checks.

| Requirement | Code or check | Software evidence | Physical evidence |
| --- | --- | --- | --- |
| Startup readiness | [Application startup](../../app/src/main.c) | `startup.hosted`: `Hil / Boot / Hosted Image Reports Boot And Ready` | `startup.l496`: `Hil / Boot / NUCLEO Reports Boot And Ready` |
| Saved parameters survive restart | [PARAM shell](../../app/src/shell/shell_param.c) | `parameters.persistence.hosted`: `Hil / Terminal / KFSW Linux Persists Parameters Across Restart` | Silicon reset and flash endurance are outside this selection. |
| `health.supervision`: an overdue component stops watchdog feeds | [Health registration](../../app/src/main.c), [unit cases](../unit/services_health/src/main.c) | `kfsw.services.health`: `services_health.test_a_component_is_late_before_it_is_overdue` checks deadlines and overdue reporting only. This unit suite has no watchdog and does not verify feed withdrawal. | [health-reset.sh](../hil/health/health-reset.sh) checks the watchdog reset and table 31 slots; standalone, outside the Robot gate. |
| Reset reason and log numbering survive reset | [Reset case](../hil/diagnostics-reset.robot) | native_sim re-executes; it cannot prove retained RAM. | `reset.retention.l496`: `Hil / Diagnostics-Reset / A Reset Leaves Its Reason And The Log Behind` |

The map records obligations, not current PASS claims. Physical evidence remains
NOT RUN until an observed bench run records its image, device identities and raw
results. A passing software selection does not cover health HIL or reset retention.
The L496 selection covers only its two named Robot cases; health's standalone
report must be checked separately. Do not use a dry-run report as execution
evidence. This counter checks report structure and identities; it does not attest
to report provenance, hardware identity, freshness or source revision.

## Reference image configurations

`reference-minimal.conf` selects PARAM with persistence, LittleFS, FTP, both
update entry points, CSP, command/status control and bounded diagnostic logs.
`reference-obc.conf` adds tables, events and the persistent journal, HK with
beacons and storage, FBO schedules, limits, ground watchdog, resource monitoring
and the existing remote-command allowlist. Both keep the bench debug shell,
use CSP v2 and exclude device demos, test parameters and semihosting.

The fragments apply after the target's transport and boot fragments. On L496,
retain the existing MCUboot, Golden and boot-budget arrangement. The OBC image
also retains its scrub selection; the minimal image disables scrub. Hosted
companions have emulated flash and no MCUboot activation, hardware watchdog or
retained reset note. The declared differences are checked in
[profiles/reference.json](profiles/reference.json).

From the workspace root, build the hosted companions in separate directories:

```bash
set -e
P="$PWD/k-fsw/config/profiles"
B="$PWD/build/reference-images"
for profile in minimal obc; do
  KFSW_OUTPUT_ROOT="$B/$profile-hosted" \
  KFSW_BUILD_DIR="$B/$profile-hosted/linux" \
  KFSW_EXTRA_CONF_FILE="$P/reference-$profile.conf" \
    k-fsw/tools/build.sh linux
  python3 k-fsw/tools/ci/profile-config.py \
    "$B/$profile-hosted/linux/zephyr/.config" --profile "$profile-hosted"
done
```

For signed L496 images, use the existing partition overlays and signing key:

```bash
set -e
for profile in minimal obc; do
  KFSW_OUTPUT_ROOT="$B/$profile-l496" \
  KFSW_BUILD_DIR="$B/$profile-l496/image" \
  KFSW_SYSBUILD=1 \
  KFSW_MCUBOOT_KEY="$HOME/.config/kfsw/mcuboot-signing-key.pem" \
  KFSW_EXTRA_CONF_FILE="$P/nucleo-mcuboot.conf;$P/nucleo-mcuboot-fwu.conf;$P/nucleo-mcuboot-fwu-lite.conf;$P/nucleo-can.conf;$P/reference-$profile.conf" \
  KFSW_EXTRA_DTC_OVERLAY_FILE="$P/nucleo-mcuboot-flash.overlay;$P/nucleo-mcuboot.overlay;$P/nucleo-mcuboot-fwu.overlay;$P/nucleo-can.overlay" \
  KFSW_MCUBOOT_DTC_OVERLAY_FILE="$P/nucleo-mcuboot-flash.overlay" \
    k-fsw/tools/build.sh nucleo_l496zg
  python3 k-fsw/tools/ci/profile-config.py \
    "$B/$profile-l496/image/app/zephyr/.config" --profile "$profile-l496"
done
```

These commands build only. They do not flash. Set a numeric
`CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION`, such as `"0.1.0+1"`, in a final extra
configuration fragment when a campaign needs a particular MCUboot image
version. `KFSW_IMAGE_VERSION` sets that signing version only in a release build.
Preserve the actual `.config`, ELF, signed image and bootloader hashes with the
build logs. Keep private signing keys out of evidence bundles.

The checker requires every declared setting, including `CONFIG_BOARD_TARGET`,
to match the resolved `.config`.
It rejects duplicate or malformed input and reports all value mismatches.
Disabled or absent symbols match `n`; `y` requirements must be present. Strings
and numbers match their exact Kconfig serialization. This proves the declared
selection, not that disabled symbols still exist in Kconfig, image provenance,
startup readiness or service behavior. The `minimal-hosted`, `obc-hosted`,
`minimal-l496` and `obc-l496` names identify distinct compositions.

Keep runtime results bound to the executable actually run. The existing PARAM
fixture fragment adds `test_u32` and therefore creates a separate bench image.
The updater composition tests also build their own images. Their results do
not prove that a reference image passed those cases. A complete acceptance run
still needs PARAM validation and restart persistence, filesystem and FTP error
paths, both update paths, trial/confirm/revert where applicable, and separate
idle/loaded results. Updates need baseline, candidate and post-boot identities.
Board tests also require observed CAN and UART/KISS results and restoration of
the saved initial state. Hosted results cannot supply that physical evidence.
