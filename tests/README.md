# K-FSW Tests

Host and HIL test runners. Developer build, run, flash, debug, and serial
utilities remain in `tools/`.

Run test scripts from the west workspace root, for example:

```bash
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/valgrind.sh
./k-fsw/tests/shell-smoke.sh
./k-fsw/tests/csp-smoke.sh
```

`tools/ci/unit.sh` runs the ztest suites under `tests/unit/` through Zephyr
Twister on `native_sim/native/64`. Results and logs are kept under
`build/twister/` by default; set `KFSW_TWISTER_OUT_DIR` to override it.
`tools/ci/valgrind.sh` runs a bounded KFSW-Linux boot under Memcheck and keeps
its logs under `build/valgrind/`.

Current:

- platform monotonic-time ztests
- CSP pre-initialization state and error-contract ztests
- native K-FSW shell smoke test
- two-node native CSP/KISS integration test
- NUCLEO boot/readiness HIL smoke test
- physical FTDI-to-NUCLEO CSP UART HIL test

Planned:

- fake clocks for deterministic time injection
- fake devices
- SocketCAN/vcan
- ZMQ simulation
- fault injection
- telemetry inspection
- soak tests
