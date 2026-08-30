# K-FSW Tests

Host and HIL test runners. Developer build, run, flash, debug, and serial
utilities remain in `tools/`.

Run test scripts from the west workspace root, for example:

```bash
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/integration.sh
./k-fsw/tools/ci/valgrind.sh
./k-fsw/tools/ci/robot.sh
./k-fsw/tests/param-local-smoke.sh
./k-fsw/tools/ci/docs.sh
./k-fsw/tools/ci/all.sh
```

`tools/ci/unit.sh` runs the ztest suites under `tests/unit/` through Zephyr
Twister on `native_sim/native/64`. Results and logs are kept under
`build/twister/` by default; set `KFSW_TWISTER_OUT_DIR` to override it.
`tools/ci/valgrind.sh` runs a bounded KFSW-Linux boot under Memcheck and keeps
its logs under `build/valgrind/`.
`tools/ci/integration.sh` builds the two native nodes and executes all shell,
CSP, PARAM, persistence, storage, and FTP integration scripts.
`tools/ci/robot.sh` validates every Robot suite and then runs all scenarios
except those tagged `physical`. `tools/ci/all.sh` composes these software-only
checks with the build, quality, unit, memory, and documentation gates.

`tests/param-local-smoke.sh` builds a test-only native composition with local
parameters and persistence enabled while both the CSP parameter adapter and CSP
itself are disabled. It exercises local list/get/set validation, defaults, and
snapshot save/load/clear without adding another supported product target.

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

## Robot Framework system and HIL tests

Robot provides operator-level system scenarios and wraps the proven physical
smoke scripts under `tests/hil/`; it does not duplicate their shell, flash,
serial, or PTY bridge control. Install its pinned Python dependency and
validate every suite without hardware:

```bash
pip install -r ./k-fsw/tests/hil/requirements.txt
./k-fsw/tests/hil/run.sh --dryrun
```

Execute every software-compatible scenario, including shell, CSP, PARAM,
parameter persistence, storage, and FTP, while excluding physical HIL:

```bash
./k-fsw/tests/hil/run.sh --exclude physical
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
`build/robot/` by default. The compact tag vocabulary is `smoke`, `software`,
`physical`, `terminal`, `shell`, `csp`, `uart`, `param`, `storage`, `ftp`, and
`persistence`. Hosted CI dry-runs all suites and executes tests selected by
`--exclude physical`; it never needs a serial device or development board.

The shell-only board profiles share one manual physical acceptance script:

```bash
./k-fsw/tests/hil/shell-smoke.sh frdm_k64f
./k-fsw/tests/hil/shell-smoke.sh rpi_pico_w
```

Each target descriptor supplies its Zephyr board, flash USB identity, runner,
console fixture, baud rate, and expected prompt. The script builds, flashes,
and checks the prompt plus `status`, `version`, and `help` without requiring
CSP or storage. After a host-side Pico UF2 copy and usbipd reattachment, set
`KFSW_FLASH=0` to run the same checks against the already-flashed application.

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
