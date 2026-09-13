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

## Bench tests

Physical tests are opt-in. Check the board, wiring, bitrate, and power before
running a fixture; some fixtures flash or reboot the target.

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
