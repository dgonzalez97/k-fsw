# Tests {#testing}

Run the checks that cover the change. From the workspace root:

```bash
./k-fsw/tools/ci/quality.sh
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/integration.sh
```

`tools/ci/all.sh` runs the software sequence. Individual entry points also cover
Robot, Valgrind, UBSan, coverage, and Doxygen. Test output stays in the build
directory.

For a focused native suite:

```bash
./k-fsw/tools/ci/unit.sh -s kfsw.services.fwu
```

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
