# K-FSW Tests

Host and HIL test runners. Developer build, run, flash, debug, and serial
utilities remain in `tools/`.

Run test scripts from the west workspace root, for example:

```bash
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/valgrind.sh
./k-fsw/tests/shell-smoke.sh
./k-fsw/tests/csp-smoke.sh
./k-fsw/tests/storage-smoke.sh
./k-fsw/tests/param-persistence-smoke.sh
```

`tools/ci/unit.sh` runs the ztest suites under `tests/unit/` through Zephyr
Twister on `native_sim/native/64`. Results and logs are kept under
`build/twister/` by default; set `KFSW_TWISTER_OUT_DIR` to override it.
`tools/ci/valgrind.sh` runs a bounded KFSW-Linux boot under Memcheck and keeps
its logs under `build/valgrind/`.

Current:

- platform monotonic-time ztests
- CSP pre-initialization state and error-contract ztests
- parameter lifecycle, validation, and serialization ztests
- storage lifecycle, format-policy, information, and file-operation ztests
- native K-FSW shell and local-parameter smoke test
- native LittleFS create/write/read/overwrite/delete integration test
- LittleFS persistence across two separate native_sim executions
- explicit parameter save/load/defaults/clear semantics across native processes
- corrupt parameter-snapshot boot fallback without filesystem corruption
- two-node native CSP/KISS parameter integration test
- K-FSW FTP protocol/path/CRC/atomic-commit ztests
- two-node CSP/RDP file transfer for zero-byte through 8 KiB files
- LIST/STAT/PUT/GET, byte comparison, missing-file, and traversal checks
- Robot operator-level remote file transfer through the KFSW-Linux shell
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
`uart.robot` verifies the debug shell, `status`, bidirectional CSP ping,
and UART transport checks through `uart-csp-smoke.sh`. Reports are written to
`build/robot/` by default. Tags are `smoke`, `nucleo`, `shell`, `csp`, `uart`,
`physical`, `terminal`, `storage`, `param`, and `persistence`.

### Terminal runner submodule

[`robot-terminal-runner`](https://github.com/dgonzalez97/robot-terminal-runner)
is an external Git submodule at `tests/platform/robot-terminal-runner`. It
provides the generic process/terminal interaction used by Robot to operate the
KFSW-Linux console and, later, the physical NUCLEO shell. Initialize it after
cloning K-FSW:

```bash
git submodule update --init --recursive
```

The terminal-tagged tests control a two-node native K-FSW CSP setup through the
KFSW-Linux shell. They cover an operator-style `csp ping 2`, remote
parameter get/set/readback, invalid-name handling, read-only rejection, and
observable `storage info` / `storage test` behavior.

The same two-node terminal setup exercises the root `ftp` command, including
`ftp <node> ...` syntax, over CSP/RDP. It generates a local exchange file,
creates a remote directory, uploads, lists/stats, downloads, compares the
returned bytes, and checks missing-file and traversal errors. The default
`tests/csp-smoke.sh` run includes zero-byte, single-packet, multi-packet, and
8 KiB transfers, then verifies CSP ping, PARAM access, and CSP buffer recovery.

The terminal suite also starts KFSW-Linux with an isolated persistent flash
image, saves a parameter, restarts the simulator process, verifies automatic
restore, and checks the distinct `defaults`, `load`, and `clear` semantics.

```bash
./k-fsw/tests/hil/run.sh --include terminal
```

Planned:

- fake clocks for deterministic time injection
- fake devices
- SocketCAN/vcan
- ZMQ simulation
- fault injection
- telemetry inspection
- soak tests
