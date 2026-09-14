# K-FSW Tests

Test scripts for the host and for hardware. Build, flash, debug and serial
tools are in `tools/`.

Run them from the west workspace root:

```bash
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/integration.sh
./k-fsw/tools/ci/valgrind.sh
./k-fsw/tools/ci/robot.sh
./k-fsw/tests/param-local-smoke.sh
./k-fsw/tools/ci/docs.sh
./k-fsw/tools/ci/all.sh
```

- `tools/ci/unit.sh` runs the ztest suites in `tests/unit/` with Twister on
  `native_sim/native/64`. Results go to `build/twister/`, or to
  `KFSW_TWISTER_OUT_DIR`.
- `tools/ci/valgrind.sh` boots KFSW-Linux under Memcheck and keeps the logs in
  `build/valgrind/`.
- `tools/ci/integration.sh` builds the flight, ground and three-node routing
  images and runs the shell, CSP, parameter, persistence, storage, FTP,
  multi-KISS, housekeeping and k-ground scripts.
- `tools/ci/robot.sh` checks every Robot suite and runs the cases that are not
  tagged `physical`.
- `tools/ci/all.sh` runs all of these plus the build, quality and
  documentation checks.

`tests/param-local-smoke.sh` builds a native image with local parameters and
persistence but without CSP, and checks list, get, set, defaults, save, load
and clear.

## Robot Framework

Robot runs the system scenarios and wraps the hardware scripts in `tests/hil/`.
Install its dependency and check every suite without hardware:

```bash
pip install -r ./k-fsw/tests/hil/requirements.txt
./k-fsw/tests/hil/run.sh --dryrun
```

Run all the software scenarios:

```bash
./k-fsw/tests/hil/run.sh --exclude physical
```

Run the hardware smoke suite with explicit device paths:

```bash
KFSW_DEBUG_SERIAL=/dev/serial/by-id/usb-STLINK_DEVICE-if02 \
KFSW_FTDI_DEVICE=/dev/serial/by-id/usb-FTDI_DEVICE-if00-port0 \
./k-fsw/tests/hil/run.sh --include smoke
```

`boot.robot` checks `@BOOT` and `@READY` with `hil-smoke.sh`. `uart.robot`
checks the shell, `status`, CSP ping in both directions and the UART test with
`uart-csp-smoke.sh`. Reports go to `build/robot/`. The tags are `smoke`,
`software`, `physical`, `terminal`, `shell`, `csp`, `uart`, `param`,
`storage`, `ftp` and `persistence`, plus `holybro`, `radio` and `raw` for the
radio tests. CI runs the dry run and the cases selected by
`--exclude physical`, so it never needs a board.

The Holybro radio has two scripts. The raw one flashes a test peer on the
NUCLEO and exchanges bytes without CSP. The CSP/KISS one builds the 57600-baud
NUCLEO and ground images and checks the module status, routes, ping in both
directions, parameters, error cases and counters. See
`tests/hil/radio-uhf/holybro/README.md`.

The shell-only boards share one script:

```bash
./k-fsw/tests/hil/shell-smoke.sh frdm_k64f
./k-fsw/tests/hil/shell-smoke.sh rpi_pico_w
```

Each target file gives the Zephyr board, flash USB ID, runner, console, baud
rate and prompt. The script builds, flashes, and checks the prompt, `status`,
`version` and `help`. After copying a Pico UF2 by hand, set `KFSW_FLASH=0` to
skip flashing.

### Terminal runner

[`robot-terminal-runner`](https://github.com/dgonzalez97/robot-terminal-runner)
is a submodule at `tests/platform/robot-terminal-runner`. Robot uses it to
drive the KFSW-Linux console. Initialize it after cloning:

```bash
git submodule update --init --recursive
```

The terminal tests run two native nodes over CSP. They cover `csp ping 2`,
remote parameter get, set and read-back, unknown names, read-only parameters,
`storage info`, `storage test`, and the `ftp` command: create a file and a
remote directory, upload, list, stat, download, compare, and try a missing file
and path traversal. `tests/csp-smoke.sh` also transfers files from zero bytes
to 8 KiB and checks ping, parameters and CSP buffers afterwards.

The terminal suite also restarts a node with a persistent flash file, checks
that saved parameters come back, and checks `defaults`, `load` and `clear`.

```bash
./k-fsw/tests/hil/run.sh --include terminal
```
