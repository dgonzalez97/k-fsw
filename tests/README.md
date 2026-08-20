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

## Robot Framework HIL

Robot wraps the proven physical smoke scripts under `tests/hil/`; it does not
duplicate their flash, serial, or PTY bridge control. Install its pinned Python
dependency and validate suite discovery without hardware:

```bash
pip install -r ./k-fsw/tests/hil/requirements.txt
./k-fsw/tests/hil/run.sh --dryrun
```

Run the smoke-tagged physical suite with explicit device paths:

```bash
KFSW_DEBUG_SERIAL=/dev/ttyACM0 \
KFSW_FTDI_DEVICE=/dev/serial/by-id/usb-FTDI_DEVICE-if00-port0 \
./k-fsw/tests/hil/run.sh --include smoke
```

`boot.robot` verifies `@BOOT` and `@READY` through `hil-smoke.sh`.
`uart.robot` verifies the debug shell, `kfsw status`, bidirectional CSP ping,
and UART transport checks through `uart-csp-smoke.sh`. Reports are written to
`build/robot/` by default. Tags are `smoke`, `nucleo`, `shell`, `csp`, `uart`,
and `physical`.

Planned:

- fake clocks for deterministic time injection
- fake devices
- SocketCAN/vcan
- ZMQ simulation
- fault injection
- telemetry inspection
- soak tests
