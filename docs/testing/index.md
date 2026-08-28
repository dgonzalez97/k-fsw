# Testing {#testing}

## Test strategy

The checks are split by scope. Source checks and unit tests catch local defects,
native-simulator tests exercise the application, and Robot covers workflows
visible at the shell. Interfaces that need a board or physical link use HIL.

GitHub Actions calls the same project scripts that developers run from the west
workspace root.

## PR gates and local equivalents

| Required check | Local command | Coverage |
| --- | --- | --- |
| `BUILD / linux` | `west manifest --validate && ./k-fsw/tools/ci/build.sh linux` | KFSW-Linux image |
| `BUILD / linux_uart` | `west manifest --validate && ./k-fsw/tools/ci/build.sh linux_uart` | Linux UART/KISS image |
| `BUILD / nucleo_l496zg` | `west manifest --validate && ./k-fsw/tools/ci/build.sh nucleo_l496zg` | Base NUCLEO image |
| `BUILD / nucleo_l496zg_uart` | `west manifest --validate && ./k-fsw/tools/ci/build.sh nucleo_l496zg_uart` | NUCLEO UART/KISS image |
| `QUALITY / clang-format + cppcheck` | `./k-fsw/tools/ci/quality.sh` | Formatting and project-owned C analysis |
| `UNIT / Twister` | `./k-fsw/tools/ci/unit.sh` | ztest suites on native simulation |
| `INTEGRATION / software` | `./k-fsw/tools/ci/integration.sh` | Shell, CSP, PARAM, persistence, storage, and FTP |
| `MEMORY / Valgrind` | `./k-fsw/tools/ci/valgrind.sh` | Normal boot and corrupt-snapshot fallback under Memcheck |
| `ROBOT / dry-run + software` | `./k-fsw/tools/ci/robot.sh` | All-suite validation plus operator software scenarios |
| `DOCS / Doxygen` | `./k-fsw/tools/ci/docs.sh` | Warning-free manual/API HTML generation |

Run all software checks with:

```bash
./k-fsw/tools/ci/all.sh
```

Dependencies beyond the Zephyr environment are Doxygen, clang-format,
cppcheck, Valgrind, socat, tmux, and the pinned Robot Framework requirement.
The scripts fail with direct messages when a required local tool is missing.

## Unit tests

`tools/ci/unit.sh` runs project-owned ztest suites with Twister on
`native_sim/native/64`. Current suites cover monotonic time, storage lifecycle
and operations, CSP error contracts, parameters, persistence, and FTP protocol
behavior. Reports are written to `build/twister/`.

## Integration tests

`tools/ci/integration.sh` builds pristine `linux` and `linux_node2` images,
then runs:

- `shell-smoke.sh` for boot markers, root shell commands, local PARAM, storage,
  and logging;
- `storage-smoke.sh` for LittleFS operations and cross-process persistence;
- `param-persistence-smoke.sh` for save/load/defaults/clear and corrupt-snapshot
  fallback;
- `csp-smoke.sh` for two-node KISS, bidirectional ping, remote PARAM, RDP FTP,
  CRC/byte comparison, invalid paths, and buffer recovery.

Temporary flash images and processes are isolated and removed by the runners.

## Valgrind

`tools/ci/valgrind.sh` performs a bounded KFSW-Linux boot and a second boot with
a corrupted parameter snapshot. Definite and indirect leaks are errors. Logs
remain under `build/valgrind/`.

## Robot Framework

Robot is the system/operator layer. It sends shell commands and asserts
visible results through the existing terminal adapter and
robot-terminal-runner. It does not contain CSP, FTP, parameter, filesystem, or
terminal-control implementation logic.

Hosted CI runs these two Robot commands:

```bash
./k-fsw/tests/hil/run.sh --dryrun
./k-fsw/tests/hil/run.sh --exclude physical
```

The first command validates discovery and syntax for every suite without
executing actions. The second executes software-tagged terminal scenarios for
shell, CSP, PARAM, persistence, storage, and FTP. Reports are separated under
`build/robot/dry-run/` and `build/robot/software/` by the CI wrapper.

## Physical HIL

Tests tagged `physical` are never executed by hosted CI. They require one or
more of ST-LINK, a NUCLEO-L496ZG, `/dev/ttyACM*`, an FTDI UART, or
`/dev/ttyUSB*`/stable `by-id` paths.

Run them locally only with an explicit bench setup:

```bash
KFSW_DEBUG_SERIAL=/dev/ttyACM0 \
KFSW_FTDI_DEVICE=/dev/serial/by-id/<ftdi-device> \
  ./k-fsw/tests/hil/run.sh --include physical
```

No CAN device or physical target is required by `tools/ci/all.sh`.
