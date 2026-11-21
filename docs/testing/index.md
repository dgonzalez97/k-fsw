# Tests {#testing}

Run the checks that cover the change. From the workspace root:

```bash
./k-fsw/tools/ci/quality.sh
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/integration.sh
```

Before a commit, run `./k-fsw/tools/ci/all.sh`. It defaults to
`--suite software`: the Zephyr patch check, the manifest, the CI builds, the
ICD check, the golden recovery builds (`golden.py`), then `quality.sh`,
`unit.sh`, `coverage.sh`, `integration.sh`, `csp-v1.sh`, `valgrind.sh`,
`robot.sh` and `docs.sh`.

| Stage | Also runs |
| --- | --- |
| `quality.sh` | `tests/build-options-test.sh` and `tests/ci-suite-test.py` (the build and CI scripts against stubs), the synthetic F746 proof tests, ruff, clang-format, the description and health slot checks, cppcheck |
| `unit.sh` | Twister on both native widths over `tests/unit` and `kfsw-modules/tests`, then the FWU recovery guard and restart check |

The exit status of `all.sh` is the verdict. Every stage prints
`<STAGE> RESULT: PASS`, and `set -Eeuo pipefail` stops the run at the first
failure, so a log that ends mid-stage failed there. Doxygen writes its
warnings to `build/docs/doxygen-warnings.log`, not the console, with
`WARN_AS_ERROR` set: a failing documentation build stops before its result
line with nothing unusual on screen.

For a focused native suite:

```bash
./k-fsw/tools/ci/unit.sh -s kfsw.services.fwu
```

### Both native widths

Twister runs `native_sim/native/64` and the 32-bit `native_sim/native`. They
differ in pointer width, type alignment and structure padding, which is where
a vendored library's assumptions show up.

`kfsw.services.param_replies` links the vendored libparam, whose `param_list.c`
derives the record stride from `__alignof__(param_t)` and asserts it is 8.
`param_t` leads with a `uint64_t`, 8-aligned on x86-64 and 4-aligned under
`-m32`, so the suite failed to compile on the 32-bit target until the fork
pinned in `west.yml` set `_Alignas(8)` on that member.

Run one suite on one width with `-p`:

```bash
./k-fsw/tools/ci/unit.sh -s kfsw.services.param_replies -p native_sim/native
```

### CSP 1

`tools/ci/csp-v1.sh` repeats the CSP checks under CSP 1: the NUCLEO builds with
and without CAN, an address CSP 1 cannot carry fails the build, the CSP and
multi-KISS smokes pass, and a CSP 2 node gets no answer from a CSP 1 node. It
also runs the FWU lite and FTP update smokes under CSP 1 on clean and lossy
links, checking the receiver's CRC32 and byte count, 105 blocks for the
20017-byte image, and retransmissions after loss. Robot's `fwu` suite has the
same exchanges tagged `software` and `csp1`, checked on the remote PARAM
replies the ground received. CSP 1 has five address bits, so the fixtures use
exact-node routes instead of `/14`, and separate `-csp1` builds. Unit suites that depend on
the address width have a `.csp1` scenario. CSP 1 is a compatibility option and
gets less exercise than the default CSP 2.

## Required evidence

`tests/requirements/` maps a few requirements to named test cases.
`software.json` selects two hosted Robot cases (boot and ready, parameters
across a restart); `l496.json` selects two physical ones on the NUCLEO (boot
and ready, the reset reason and log across a reset).
`tools/ci/robot-counts.py` checks a Robot report against a selection: each
selected case must appear exactly once, with PASS. The Robot CI job checks
`software.json` after its scenarios run:

```bash
python3 k-fsw/tools/ci/robot-counts.py build/robot/software/output.xml \
  --required k-fsw/tests/requirements/software.json
```

The board stage does not apply `l496.json`; check it by hand on the board
report:

```bash
python3 k-fsw/tools/ci/robot-counts.py build/robot/board/board/output.xml \
  --board --required k-fsw/tests/requirements/l496.json
```

The optional hash binding of a report to its image and `.config` is in
`tests/requirements/image-binding.md`; CI does not use it.

## Analysis

These stages are not part of `all.sh`. Run them before a release or after a
change to memory handling. Each prints `<STAGE> RESULT: PASS` or `FAIL` and
keeps its output under `build/`.

| Stage | Finds | Time on a desktop |
| --- | --- | --- |
| `sca.sh` | GCC's `-fanalyzer`: null dereferences, leaks, use after free, along paths through the code. The NUCLEO image, plus the services only Linux carries, built with the board's compiler | under a minute |
| `asan.sh` | AddressSanitizer and LeakSanitizer over the unit suites, 64-bit native_sim | about 5 minutes |
| `codechecker.sh` | clang-tidy through CodeChecker, the same sources as `sca.sh`; HTML report in `build/codechecker/html` | under a minute |
| `sbom.sh` | SPDX 2.3 bill of materials for the NUCLEO image, in `build/sbom/spdx` | under a minute |
| `ubsan.sh` | Undefined behaviour in the unit suites | about 5 minutes |
| `footprint.sh` | Flash and RAM per symbol, see @ref footprint | under a minute |

`sca.sh` and `codechecker.sh` fail on a finding in a K-FSW repository; Zephyr,
its modules and the libcsp and libparam forks are left out. A finding reviewed
as a false positive is marked where it is, with a `codechecker_false_positive`
or `codechecker_intentional` comment and the reason. CodeChecker needs
`./.venv/bin/pip install codechecker clang-tidy`.

Not in place yet: clang as a second compiler and sparse, which need system
packages, and LTO, which shrinks the NUCLEO image by about 20 KB but needs
`CONFIG_SHARED_INTERRUPTS=n` (no interrupt is shared today) and a run on the
board before it changes a flight image.

## Linux diagnostics

The integration stage includes the ground-tool tests and the HK capture and
replay fixture. To add the optional CSP tools fork, from `k-fsw`:

```bash
tools/kfsw-linux build
tools/kfsw-linux csp-tools build
tools/kfsw-linux diagnostics ../build/diagnostics/run-1
```

The fixture starts its own native node and keeps its console, flash file and
captures in the new output directory. Focused tests
are `tests/ground/` and the Twister suite `kfsw.comms.ifstats`. Robot's
`diagnostics` suite has software cases and one physical case, not yet run;
`tests/hil/diagnostics/README.md` lists its bench inputs and side effects.

## Bench tests

Physical tests are opt-in, and some flash or reboot the target. Check the
board, wiring, bitrate and power first.

1. Ask the bench what it can serve:

   ```bash
   ./k-fsw/tests/hil/preflight.sh
   ```

   It reports the debug UART, a second serial adapter, a radio port, a CAN
   interface that is up, and which of the six bench shapes can run.
2. Run one shape:

   ```bash
   ./k-fsw/tests/hil/run.sh board
   ```

The shapes are `software`, `terminal`, `board`, `board-uart`, `board-can` and
`radio`; `tests/README.md` says what each needs and covers. A shape the bench
cannot serve is refused with the reason before it starts.

| Check | Procedure |
| --- | --- |
| CAN firmware upload, slot readback, rollback, confirmation | `tests/hil/fwu/README.md` |
| Loaded timing, soak, interrupted erase/write | `tests/hil/fwu/can-acceptance.py` |
| NUCLEO CAN setup | `tests/hil/stm32/nucleo-l496zg/` |
| Holybro serial and CSP link | `tests/hil/radio-uhf/holybro/` |
| Encrypted radio, wrong keys, captured-frame replay | `tests/hil/radio-uhf/holybro/crypto-smoke.py` |
| Image scrub: a reference across a reset, a changed slot | `tests/hil/scrub/learned-reference.sh` |
| A procedure over the bus, the queue releasing alone, remote execution | `tests/hil/fbo/pass-plan.sh` |
| A parameter limit releasing a command on two links at once | `tests/hil/limits/released-on-two-links.sh` |
| Bootloader revert and the wrong signing key | `tests/hil/mcuboot/rollback.sh` |
| Robot scenarios | `tests/hil/` |

Keep the source commits, configuration, device identities, routes, interface
counters and console logs with each result. @ref project_status has the
recorded coverage.

### The board suite

Before a tag, prepare and back up the bench, then run:

```bash
./k-fsw/tools/ci/all.sh --suite board
```

This runs the software sequence, then `tools/ci/board.sh`, which can also run
on its own. In order, it runs:

1. CAN acceptance, then encrypted radio, before the other cases overwrite
   their baselines. Each skips without its inputs.
2. Robot's `board`, `board-uart`, `board-can` and `radio` shapes.
3. The standalone watchdog, health, MCUboot rollback, Golden recovery and CAN
   limit checks (`tests/hil/limits/released-over-can.sh`).
4. The FRDM shell check, when its probe and serial path are present.
5. `tools/f746-proof.sh build`, an offline build and image check with no
   hardware IO, when `KFSW_MCUBOOT_KEY` or
   `~/.config/kfsw/mcuboot-signing-key.pem` is readable.

These cases reflash boards, rewrite firmware slots and Golden, and reset
nodes. Radio update takes tens of minutes; CAN acceptance adds an hour. Button acceptance and the Pico shell smoke always print NOT RUN because
they need a person; run them separately (@ref development).

| Case | Prepare | Supply |
| --- | --- | --- |
| CAN acceptance | Install and confirm the probes-enabled baseline in `tests/hil/fwu/README.md`; set `KFSW_FWU_CAN_ACCEPTANCE_READY=1` | `KFSW_FWU_CAN_GROUND`, `KFSW_FWU_CAN_IMAGE`, `KFSW_FWU_CAN_FLASH` (the ground flash holding `/candidate.bin`) |
| Encrypted radio | Prepare the baseline in `tests/hil/radio-uhf/holybro/README.md`; set `KFSW_RADIO_CRYPTO_READY=1` | `KFSW_RADIO_CRYPTO_GROUND`, `KFSW_RADIO_CRYPTO_KEY_FILE` |

CAN acceptance resets interrupted secondary-slot erases and writes. Encrypted
radio leaves its key configured. The usual preflight serial and CAN overrides
apply.

A case that cannot run prints NOT RUN with its reason, and the final line
names the skipped shapes and cases. Missing hardware, scripts or tools do not
fail the command; a case that runs and fails does. Record every NOT RUN in the
tag notes.
