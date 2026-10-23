# Tests {#testing}

Run the checks that cover the change. From the workspace root:

```bash
./k-fsw/tools/ci/quality.sh
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/integration.sh
```

Before a commit, run `./k-fsw/tools/ci/all.sh` from the workspace root.
It defaults to `--suite software` and runs the software sequence, including
FWU recovery guards and restart after Twister in `unit.sh`, and the synthetic
F746 proof tests in `quality.sh`.

Before a tag, prepare the bench and run:

```bash
./k-fsw/tools/ci/all.sh --suite board
```

This runs the software sequence first, then `tools/ci/board.sh`. The board
stage can also run on its own. It selects Robot's `board`, `board-uart`,
`board-can` and `radio` shapes with their existing tags and preflights, then
runs the standalone watchdog, health, MCUboot rollback, Golden recovery,
CAN limit and FRDM shell checks. Radio firmware update is already in Robot.
These checks reflash boards, rewrite firmware slots or Golden, and reset
nodes. Back up the bench first. Radio update takes tens of minutes; CAN
acceptance adds a one-hour soak when enabled.

Every unavailable case prints NOT RUN with its reason. The final board result
names skipped shapes and individual Robot cases. Missing hardware and
all-skipped shapes are never PASS. Missing hardware, case scripts or required
tools do not fail the command; a case that executes and fails does. A zero exit with NOT RUN is incomplete
bench coverage, and the tag notes must record it. Physical acceptance still
needs human-observed evidence.

CAN acceptance and encrypted radio run before the other cases overwrite their
prepared baselines. For CAN acceptance, install and confirm the probes-enabled
baseline described in `tests/hil/fwu/README.md`, set
`KFSW_FWU_CAN_ACCEPTANCE_READY=1`, and supply `KFSW_FWU_CAN_GROUND`,
`KFSW_FWU_CAN_IMAGE` and `KFSW_FWU_CAN_FLASH` (the ground flash containing
`/candidate.bin`). The test resets interrupted secondary-slot erase/write.
For encrypted radio, prepare the baseline in
`tests/hil/radio-uhf/holybro/README.md`, set `KFSW_RADIO_CRYPTO_READY=1`, and
supply `KFSW_RADIO_CRYPTO_GROUND` and `KFSW_RADIO_CRYPTO_KEY_FILE`.
It leaves the encryption key configured. Both cases skip without those inputs.
The usual preflight serial and CAN overrides still apply.

The board stage also runs `tools/f746-proof.sh build` when the explicitly set
`KFSW_MCUBOOT_KEY` names a readable file. Otherwise it prints NOT RUN. This is
an offline build and artifact check, with no hardware IO. Button acceptance
and the Pico shell smoke always print NOT RUN because they need a person;
complete them separately in the bench gate in @ref development.

Individual entry points also cover
Robot, Valgrind, UBSan, coverage, @ref footprint, and Doxygen. Test output
stays in the build directory.

Read `all.sh`'s own exit status to tell whether it passed. Every stage prints
`<STAGE> RESULT: PASS`, so a log with no `FAIL` and a tail that reaches
`DOCS RESULT` is a good sign, but the exit status is the verdict. Doxygen writes
its warnings to `build/docs/doxygen-warnings.log` rather than the console, with
`WARN_AS_ERROR` set, so a failing documentation build prints nothing unusual and
simply stops before its result line. When any stage fails, `set -Eeuo pipefail`
ends the run there and the log stops mid-stage.

`tools/ci/csp-v1.sh`, part of `all.sh`, repeats the CSP checks under CSP 1: the
NUCLEO builds with and without CAN, an address CSP 1 cannot carry fails the
build, the CSP and multi-KISS smokes pass, and a CSP 2 node gets no answer from
a CSP 1 node. It also reuses the FWU lite and FTP update smokes under CSP 1,
each with clean and lossy links: receiver CRC32 and byte count, 105 observed
blocks for 20000 bytes, and retransmissions after loss. Robot's `fwu` suite
has the same exchanges tagged `software` and `csp1`; its byte-count and CRC
assertions check remote PARAM replies received by ground, not a local send
message. Nodes 16 and 19 are legal under CSP 1 and stay unchanged; the fixtures
use exact-node routes instead of `/14` (CSP 1 has only five address bits),
and separate `-csp1` builds. These are software exchanges, not physical update
or bootloader evidence. CSP 1 is a limited compatibility build option, less
exercised than the default CSP 2. Unit suites that depend on the address width
also have a
`.csp1` scenario.

For a focused native suite:

```bash
./k-fsw/tools/ci/unit.sh -s kfsw.services.fwu
```

## Analysis

These stages are not part of `all.sh`; run them before a release or after a
change that touches memory handling. Each prints `<STAGE> RESULT: PASS` or
`FAIL` and keeps its output under `build/`.

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

The normal integration workflow includes the Python ground-tool tests and the
HK capture/replay fixture. To include the optional CSP tools fork, from `k-fsw`:

```bash
tools/kfsw-linux build
tools/kfsw-linux csp-tools build
tools/kfsw-linux diagnostics ../build/diagnostics/run-1
```

Use a new output directory. The fixture starts its own native node and keeps
its console, flash file, captures and replay checks there. Focused tests are
`tests/ground/` and Twister suite `kfsw.comms.ifstats`. Robot's `diagnostics`
suite also has software cases and a physical case; the latter remains pending.
See `tests/hil/diagnostics/README.md` for explicit bench inputs and side effects.

## Bench tests

Physical tests are opt-in. Check the board, wiring, bitrate, and power before
running a fixture; some fixtures flash or reboot the target.

Ask the bench what it can serve before flashing anything:

```bash
./k-fsw/tests/hil/preflight.sh
```

It reports the debug UART, a second serial adapter, a radio port, a CAN
interface that is up, and which of the six bench shapes can run. Then run one
shape rather than every suite:

```bash
./k-fsw/tests/hil/run.sh board
```

The shapes are `software`, `terminal`, `board`, `board-uart`, `board-can` and
`radio`; `tests/README.md` says what each needs and covers. A shape that the
bench cannot serve is refused with the reason, instead of starting and failing
partway through.

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
counters, and console logs with each result. A build or native simulation does
not establish physical timing or link behaviour. See @ref project_status for
recorded coverage and @ref targets for the supported profiles.
