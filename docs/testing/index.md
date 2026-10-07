# Tests {#testing}

Run the checks that cover the change. From the workspace root:

```bash
./k-fsw/tools/ci/quality.sh
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/integration.sh
```

`tools/ci/all.sh` runs the software sequence. Individual entry points also cover
Robot, Valgrind, UBSan, coverage, @ref footprint, and Doxygen. Test output
stays in the build directory.

`tools/ci/csp-v1.sh`, part of `all.sh`, repeats the CSP checks under CSP 1: the
NUCLEO builds with and without CAN, an address CSP 1 cannot carry fails the
build, the CSP and multi-KISS smokes pass, and a CSP 2 node gets no answer from
a CSP 1 node. Unit suites that depend on the address width also have a
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
| Robot scenarios | `tests/hil/` |

Keep the source commits, configuration, device identities, routes, interface
counters, and console logs with each result. A build or native simulation does
not establish physical timing or link behaviour. See @ref project_status for
recorded coverage and @ref targets for the supported profiles.
