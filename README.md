# K-FSW - Modular flight software on Zephyr, for small satellites

[![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-K--FSW-294c69)](https://dgonzalez97.github.io/k-fsw/)

K-FSW gives spacecraft components the common services a mission needs:
a console, ground links, parameters, files, logs, events, commands,
housekeeping, and firmware updates. Select what an OBC, radio, ADCS, EPS,
or payload needs, then add the mission-specific code.

Zephyr supplies the RTOS, drivers, board support, and build tools, so the
application can run on different MCU vendors and in Linux simulation.
Modules and services can be reused in another RTOS port by adapting the
Zephyr APIs they use.

[Documentation](https://dgonzalez97.github.io/k-fsw/) ·
[Getting started](docs/getting-started/index.md) ·
[Shell commands](docs/commands/index.md) ·
[Project status](docs/status/index.md)

## Repository layout

![K-FSW repositories](docs/media/layout.svg)

| Repository | Owns |
| --- | --- |
| [`k-fsw`](https://github.com/dgonzalez97/k-fsw) | Application, targets, tools, integration tests, docs |
| [`kfsw-modules`](https://github.com/dgonzalez97/kfsw-modules) | Device and subsystem modules |
| [`kfsw-services`](https://github.com/dgonzalez97/kfsw-services) | Logging, parameters, persistence, files, events, commands, health, firmware update |
| [`kfsw-comms`](https://github.com/dgonzalez97/kfsw-comms) | CSP, routing, UART/KISS and CAN transports |
| [`kfsw-platform`](https://github.com/dgonzalez97/kfsw-platform) | Zephyr mechanisms: time, storage, reset cause, watchdog, I/O |

## Modules

A module owns one device or subsystem: its interface, state, parameters,
and tests. Add its definition set to the application composition; the
parameter core does not need to change.

Examples in  [`kfsw-modules`](https://github.com/dgonzalez97/kfsw-modules):
a UHF radio, and a small worked example using LEDs and buttons of development boards.

## Services

- **Parameters:** named values with owner validation, remote access, and
  explicit save/load.
- **CSP:** one router, with routes selecting UART/KISS or CAN links.
- **Files:** upload and download with CRC32 and atomic commit.
- **Firmware update:** direct block upload or FTP, with MCUboot test,
  confirm, and rollback.
- **Commands and events:** typed operations and a bounded record of results.
- **Health:** component deadlines control watchdog feeding.
- **Housekeeping:** collect groups of parameters and forward samples to Yamcs.
- **Ground nodes:** the same application and services built for Linux.

## Try it on Linux

From a configured west workspace, the local demo connects Linux nodes
through pseudo-terminals using CSP/KISS.

![Building a node and bringing up a link](docs/media/getting-started.gif)

```bash
west manifest --validate
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/k-ground init
```

Start each role in its own terminal:

```bash
./k-fsw/tools/k-ground run kfsw-gnd-uhf
```

```bash
./k-fsw/tools/k-ground run kfsw-ops
```

`csp ping 16` from the operator node crosses the link and comes back. The
[ground guide](docs/ground/index.md) covers the configuration and the
other ground roles.

### Settings and configuration, across all nodes

`param tables` lists local tables; `param table <id>` prints one.
Use `param tablelist <node>` to inspect another node.

Reaching them from the ground is a separate, optional piece that speaks
[Space Inventor's libparam](https://github.com/spaceinventor/libparam).

![Reading and writing parameters across a link](docs/media/param-over-a-link.gif)

### Files

Uploads and downloads check size and CRC32 before committing the file.
CSP/RDP provides retransmission; an interrupted transfer must be restarted.

![A file sent and fetched back](docs/media/file-transfer.gif)

### Firmware update

Send, verify, flash, reboot, confirm. MCUboot reverts an unconfirmed test
image on the next reset. The direct block protocol separates upload from
`fwu flash`; the FTP route schedules the swap after verification.

A flash region is reserved for a recovery image. Recovery-image boot
selection is not implemented. See the [update guide](docs/fwu/README.md).

![Firmware update over a radio link](docs/media/firmware-update-over-radio.gif)

## Hardware

NUCLEO-L496ZG is the current MCU reference. Recorded bench runs cover
parameters, files, commands, events, CAN, housekeeping, and firmware updates.
Some features require opt-in profiles; see [targets](docs/targets/index.md).

[Project status](docs/status/index.md) records the tested configurations
and remaining hardware checks.

## Targets

| Target | Board | Links | What runs on it |
| --- | --- | --- | --- |
| `linux` | `native_sim/native/64` | KISS over a PTY; optional CAN via SocketCAN | Reference services and optional profiles |
| `nucleo_l496zg` | STM32 Nucleo L496ZG | KISS on USART3; optional CAN on PD0/PD1 | Reference services and optional profiles |

FRDM and Pico W currently provide shell bring-up profiles. CSP,
parameters, storage, and file transfer are disabled.

| Target | Board | Verified scope |
| --- | --- | --- |
| `frdm_k64f` | `frdm_k64f/mk64f12` | OpenSDA UART shell |
| `rpi_pico_w` | `rpi_pico/rp2040/w` | USB CDC ACM shell |

## Mission control

[Yamcs](https://yamcs.org/) records housekeeping telemetry. The
[kfsw-yamcs](https://github.com/dgonzalez97/kfsw-yamcs) configuration is a
submodule under `ground-station/yamcs`:

```bash
cd ground-station/yamcs
./mvnw yamcs:run
```

The host bridge pulls CSP samples and forwards them to Yamcs. Report
definitions generate both the node configuration and the mission database.
Configure housekeeping through K-FSW commands; Yamcs currently records
telemetry only.

The [ground guide](docs/ground/index.md) has the walkthrough, including how to
check it before any hardware is involved.

## Testing

The software workflow runs these checks:

| Job | What it checks |
| --- | --- |
| `BUILD / linux`, `BUILD / nucleo_l496zg` | Both full targets build, plus a CSP-disabled composition |
| `QUALITY` | clang-format and cppcheck over the sources |
| `UNIT / Twister` | Component tests on native simulation |
| `INTEGRATION / software` | Smoke scripts driving full images |
| `MEMORY / Valgrind` | The hosted image under Valgrind |
| `UNDEFINED / UBSan` | Unit suites with undefined-behaviour checks |
| `ROBOT / dry-run + software` | Every suite parses; the software-tagged cases run |
| `DOCS / Doxygen` | The documentation builds and the API is documented |

The unit suites cover each layer on its own; the integration scripts go the
other way, booting a real image and talking to it through a ground node the way
an operator would.

[Coverage](https://dgonzalez97.github.io/k-fsw/coverage/) reports unit-test
lines, functions, and branches. Integration and HIL runs are not included.

### Hardware in the loop

[Robot Framework](https://robotframework.org/) drives the physical suites
through [robot-terminal-runner](https://github.com/dgonzalez97/robot-terminal-runner),
a submodule under `tests/platform/`. It sends shell commands through tmux
and records the results.

| Suite | Needs | Covers |
| --- | --- | --- |
| `boot`, `uart` | Nucleo | Boot markers, reset cause, shell and CSP over the debug UART |
| `param-tables` | Nucleo | Every table present and addressed, with the right write modes |
| `can` | Nucleo + CAN adapter | CSP over CAN: ping, identity and remote settings |
| `holybro` | Nucleo + radio pair | CSP, files, commands and events across the link |
| `fwu` | Nucleo + CAN adapter | FTP and FWU lite uploads, slot readbacks, rollback and confirmation |

Cases needing the board are tagged, so the same files run in CI without
hardware and on the bench with it. The [testing guide](docs/testing/index.md)
links to the fixtures and commands.

The parameter-table suite running against the Nucleo over CAN and RF.

![Hardware test suite running](docs/media/hardware-test-robot.gif)

And the report it leaves behind:

![Robot report from a physical run](docs/media/hil-robot-report.png)

The [status page](docs/status/index.md) records observed physical results
separately from software tests.

## Development

The [contribution guide](docs/development/index.md) covers repository
ownership, dependency pins, and PRs. Build the HTML and PDF from the workspace:

```bash
./k-fsw/tools/docs/build.sh
./k-fsw/tools/docs/pdf.sh
```
