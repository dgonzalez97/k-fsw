# K Flight Software — K-FSW

K-FSW is an open-source flight-software framework for satellites and satellite
subsystems. The application runs on Zephyr RTOS and composes reusable platform,
service, communications, and subsystem modules from the west workspace.

## Architecture

`k-fsw` is the composition repository. Board profiles select configuration and
hardware backends without replacing the application or its modules.

KFSW-Linux is the `native_sim/native/64` profile of that same application. It is
a first-class development and operations node, not a separate flight-software
implementation or a POSIX rewrite. Its local Zephyr shell provides a convenient
command console while the normal K-FSW services and communications code remain
in use.

Filesystem storage follows the same rule. Both NUCLEO and KFSW-Linux mount
LittleFS at `/kfsw` through the `kfsw-platform` lifecycle and Zephyr's flash
map. KFSW-Linux uses native_sim's persistent simulated-flash file rather than a
host-only `fopen()` backend.

```text
local kfsw shell
       |
       v
K-FSW service/comms APIs
       |
       v
     libcsp
       |
       +-- KISS / UART       implemented
       +-- CFP / CAN         deferred
       +-- future transports
```

The shell is a user interface. Commands such as `csp ping 2` and
`param get 2 test_u32` call K-FSW APIs, which use libcsp to reach the
selected node. File, housekeeping, or command operations should follow the
same boundary and use defined CSP services; K-FSW does not send shell command
strings to remote nodes. Node 2 is used by the current demo, but generic
commands accept a target node and do not encode that topology.

## Workspace repositories

| Repository | Responsibility |
| --- | --- |
| `k-fsw` | Application composition, west manifest, profiles, tools, and integration tests |
| `kfsw-platform` | Zephyr-backed platform capabilities such as time and reset cause |
| `kfsw-services` | Reusable boot, readiness, logging, and future flight services |
| `kfsw-comms` | CSP lifecycle, routing, and transport APIs |
| `kfsw-modules` | Reusable spacecraft equipment and subsystem clients |

libcsp remains a separate upstream west project. `west.yml` pins its exact
revision and checks it out at `kfsw-comms/third_party/libcsp`; `kfsw-comms`
integrates it through the same Zephyr module composition used by every profile.
The manifest likewise pins Space Inventor libparam at
`kfsw-services/third_party/libparam`; `kfsw-services` compiles its selected core
sources directly through Zephyr/CMake, without invoking upstream Meson.

## Profiles and current status

| Profile | Purpose | Status |
| --- | --- | --- |
| `linux` | KFSW-Linux command console, CSP node 1 by default | Working; software verified |
| `linux_node2` | Second native simulator node for integration tests | Working; software verified |
| `linux_uart` | KFSW-Linux node used by the physical UART bridge test | Physically verified |
| `nucleo_l496zg` | Base NUCLEO-L496ZG image | Build and boot verified |
| `nucleo_l496zg_uart` | NUCLEO with CSP KISS on USART3 PD8/PD9 | Physically verified |

The debug shell and basic CSP routing/ping are working. KISS over UART is
implemented and has passed physical testing. CAN/CFP work is parked until a
replacement CAN transceiver is available; CAN is not currently claimed as a
working transport.

## Software CI

Run the complete software build matrix locally from the west workspace root:

```bash
./k-fsw/tools/ci/build.sh
./k-fsw/tools/ci/quality.sh
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/valgrind.sh
./k-fsw/tools/ci/all.sh
```

The matrix builds pristine `linux`, `linux_uart`, `nucleo_l496zg`, and
`nucleo_l496zg_uart` profiles. Pass one or more profile names to run a subset.
GitHub Actions uses this same entry point after recreating the pinned west
workspace and installing the Zephyr SDK selected by Zephyr 4.4. The quality
entry point checks application and test formatting with `clang-format` and
statically analyzes all project-owned C sources with `cppcheck`; install those
two tools before running it locally.

The unit entry point runs the project-owned ztest suites through Twister on
`native_sim/native/64` and keeps its reports under `build/twister/`.
The Valgrind entry point builds KFSW-Linux, boots it to `@READY`, and exits at
one simulated second; install Valgrind before running it locally. Program and
Memcheck logs remain under `build/valgrind/`.
`tools/ci/all.sh` runs all four software stages and never requires physical
hardware.

Physical HIL remains separate. Robot Framework wraps the existing smoke
scripts and accepts hardware paths through the environment:

```bash
KFSW_DEBUG_SERIAL=/dev/ttyACM0 \
KFSW_FTDI_DEVICE=/dev/serial/by-id/usb-FTDI_DEVICE-if00-port0 \
./k-fsw/tests/hil/run.sh --include smoke
```

Use `./k-fsw/tests/hil/run.sh --dryrun` for hardware-free syntax and discovery
validation. GitHub-hosted runners perform only that dry run, never physical
HIL.

## KFSW-Linux command console

From the west workspace root, build and run KFSW-Linux with:

```bash
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

`run` builds the `linux` profile automatically if its executable is missing.
It attaches the local Zephyr shell directly to the current terminal, so no
PTY discovery or `picocom` session is needed. Startup ends at:

```text
@BOOT sw=kfsw-dev board=native_sim/native/64 ...
@READY uptime_ms=...

kfsw:~$
```

The preceding `uart_1 connected to pseudotty: ...` line identifies the CSP
KISS transport, not the user shell. It is used by bridge and integration tools.
Press `Ctrl-C` to stop KFSW-Linux.

Useful commands include:

```text
status
version
csp info
csp interfaces
csp routes
csp ping <node>
param list
param get <name>
param set <name> <value>
param save
param load
param defaults
param clear
param list <node>
param get <node> <name>
param set <node> <name> <value>
storage info
storage test
ftp <node> ls [remote-directory]
ftp <node> mkdir <remote-directory>
ftp <node> put <local-path> <remote-path>
ftp <node> get <remote-path> <local-path>
```

`kfsw:~$ ` is the standard Zephyr UART shell prompt, not a command namespace.
Root commands and their static subcommands use Zephyr's built-in tab
completion, history, and line editing.

The initial table contains read-only `node_id` plus writable `log_level`,
`test_u32`, `test_i32`, and `test_float` examples. Parameter pull/push traffic
uses CSP port 10 and parameter-list traffic uses port 12; both ports are Kconfig
options. Current upstream libparam hard-codes those ports, which is the reason
K-FSW uses 10/12 instead of the older RPARAM port 7 convention.

The four writable values are explicitly persistent; read-only `node_id` is
not. `set` changes RAM only, `save` atomically replaces the snapshot at
`/kfsw/params/parameters.dat`, and a valid snapshot is restored before the CSP
parameter server starts. `defaults` restores compiled values in RAM without
deleting the snapshot, while `clear` deletes the snapshot without changing
RAM. Invalid headers, versions, lengths, or CRCs leave compiled defaults active
and do not prevent boot.

For example:

```text
param set test_u32 1234
param save
# restart K-FSW
param get test_u32
test_u32 = 1234
```

The software-only two-node CSP regression creates and connects the native KISS
PTYs automatically, pings both nodes, performs remote parameter list/get/set,
checks readback and error cases, and cleans up its processes:

```bash
./k-fsw/tests/csp-smoke.sh
```

The storage integration test creates, writes, reads, overwrites, and deletes a
small file, then writes a known value in one native_sim process and verifies it
from a second process using the same simulated flash image:

```bash
./k-fsw/tests/storage-smoke.sh
```

Parameter persistence has a separate two-execution and corruption regression:

```bash
./k-fsw/tests/param-persistence-smoke.sh
```

## NUCLEO-L496ZG

Build the normal board profile from the workspace root:

```bash
./k-fsw/tools/build.sh nucleo_l496zg
```

With a board attached through ST-LINK, flash it with:

```bash
./k-fsw/tools/flash.sh nucleo_l496zg
```

The debug shell and boot log use the ST-LINK virtual COM port at 115200 baud;
the current default is `/dev/ttyACM0`:

```bash
picocom --baud 115200 /dev/ttyACM0
```

Exit `picocom` with `Ctrl-A`, then `Ctrl-X`.

## Verified CSP UART bench demo

The physical demo connects KFSW-Linux CSP node 1 to a NUCLEO-L496ZG CSP node 2
through an FTDI TTL-232R-3V3 cable. CSP uses a dedicated UART; the NUCLEO shell
and logs remain on ST-LINK.

### Hardware and wiring

The verified FTDI device is an FT232 Serial with USB ID `0403:6001`. Configure
the link for 115200 baud, 8 data bits, no parity, one stop bit, and no flow
control.

Connect only these signals:

```text
FTDI ORANGE TXD  -> NUCLEO D0 / PD9 / USART3_RX
FTDI YELLOW RXD  <- NUCLEO D1 / PD8 / USART3_TX
FTDI BLACK GND   -> NUCLEO GND
```

Leave FTDI RED VCC, CTS, and RTS unconnected. Power and debug the NUCLEO through
ST-LINK. The two serial paths have distinct jobs:

```text
FTDI UART  -> CSP KISS data
ST-LINK    -> /dev/ttyACM0 -> Zephyr shell and logs
```

### Topology

```text
+-----------------------+
| KFSW-Linux            |
| CSP node 1            |
|                       |
| kfsw:~$               |
+-----------+-----------+
            |
          libcsp
            |
           KISS
            |
     native_sim PTY
            |
          socat
            |
    FTDI TTL-232R-3V3
            |
      115200 baud, 8N1
            |
   USART3 PD8 / PD9
            |
+-----------+-----------+
| NUCLEO-L496ZG         |
| CSP node 2            |
+-----------------------+

At the same time:

ST-LINK -> /dev/ttyACM0 -> Zephyr shell
```

### Reproduce the demo

Prefer a stable `/dev/serial/by-id/` path for the FTDI cable. The current bench
path is shown below as a working example; its serial number is not required and
must be replaced when a different cable is used.

```bash
ls -l /dev/serial/by-id/

./k-fsw/tests/uart-csp-smoke.sh \
  --ftdi /dev/serial/by-id/usb-FTDI_FT232R_USB_UART_AL05JTP2-if00-port0 \
  --serial /dev/ttyACM0
```

The test builds both UART profiles, flashes the NUCLEO, captures its ST-LINK
console, starts KFSW-Linux, bridges the native CSP PTY to the FTDI cable with
`socat`, and exercises the link in both directions. It restores terminal
settings and terminates its child processes on exit.

The verified bench result was:

- KFSW-Linux to NUCLEO: `CSP ping 2` succeeded.
- NUCLEO to KFSW-Linux: `CSP ping 1` succeeded.
- The permanent UART CSP test passed in both directions.
- The transport test used a 128-byte CSP payload with CRC32.
- Final observed KISS transmit/receive errors, frame errors, and drops were
  zero.

Observed round-trip times are diagnostic results for a particular run, not
performance guarantees.

## Regression helpers

Run these from the workspace root after the corresponding profiles are built:

```bash
./k-fsw/tests/shell-smoke.sh
./k-fsw/tests/csp-smoke.sh
```

`tests/uart-csp-smoke.sh` is a hardware-in-the-loop test that flashes the board
and requires the wired NUCLEO and FTDI setup described above.
