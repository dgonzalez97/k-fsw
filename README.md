<p align="center"><img src="docs/media/title.svg" alt="K-FSW"></p>

# K-FSW - Modular flight software on Zephyr, for small satellites

[![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-K--FSW-294c69)](https://dgonzalez97.github.io/k-fsw/)

K-FSW gives spacecraft components the common services a mission needs:
a console, ground links, parameters, files, logs, events, commands,
housekeeping, and firmware updates. Select what an OBC, radio, ADCS, EPS,
or payload needs, then only add the mission-specific code.

Zephyr supplies the RTOS, drivers, board support, and build tools, so the
application can run on different MCU vendors and in Linux simulation.
Modules and services can be reused in another RTOS port by adapting the
Zephyr APIs they use.

[Documentation](https://dgonzalez97.github.io/k-fsw/) |
[Getting started](docs/getting-started/index.md) |
[Shell commands](docs/commands/index.md) |
[Project status](docs/status/index.md)

## Repository layout

![K-FSW repositories](docs/media/layout.svg)

| Repository | Contents |
| --- | --- |
| [`k-fsw`](https://github.com/dgonzalez97/k-fsw) | Application, targets, tools, integration tests, docs |
| [`kfsw-modules`](https://github.com/dgonzalez97/kfsw-modules) | Device and subsystem modules |
| [`kfsw-services`](https://github.com/dgonzalez97/kfsw-services) | Logging, parameters, persistence, files, events, commands, health, firmware update |
| [`kfsw-comms`](https://github.com/dgonzalez97/kfsw-comms) | CSP, routing, UART/KISS and CAN transports |
| [`kfsw-platform`](https://github.com/dgonzalez97/kfsw-platform) | Zephyr mechanisms: time, storage, reset cause, watchdog, I/O |

## Modules

A module defines one device or subsystem: its interface, state, parameters,
and tests. The parameter core does not need to change.

Examples in [`kfsw-modules`](https://github.com/dgonzalez97/kfsw-modules):
a UHF radio, and a small worked example using LEDs and buttons of development boards.

## Services

- **Parameters:** named values with validation, remote access, callbacks and optional persistence.
- **CSP:** communications router, with routes selecting UART/KISS or CAN links.
- **Files:** upload and download with CRC32 and RDP and a complete file system creating tool.
- **Firmware update:** binaries over FTP, with MCUboot test,
  confirm, and rollback, or a simple direct block upload FWU over a csp line that doesnt need a file system.
- **Commands and events:** a shell and remote commanding.
- **Health:** component deadlines, control and watchdog.
- **Housekeeping:** collect groups of parameters and forward telemetry to Yamcs.
- **Ground nodes:** the same application and services built for Linux.

## KFSW on Linux

The local demo connects Linux nodes using CSP/KISS.

![Building a node and bringing up a link](docs/media/getting-started.gif)

```bash
west manifest --validate
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/k-ground init
```

```bash
./k-fsw/tools/k-ground run kfsw-gnd-uhf
```

```bash
./k-fsw/tools/k-ground run kfsw-ops
```

Run `csp ping 16` from the operator node. The
[ground guide](docs/ground/index.md) covers the configuration and the
other ground roles.

### Settings and configuration, across all nodes

`param tables` lists local tables; `param table <id>` prints one.
Use `param tablelist <node>` to inspect another node.

Remote access uses [Space Inventor's libparam](https://github.com/spaceinventor/libparam).

![Reading and writing parameters across a link](docs/media/param-over-a-link.gif)

### Files

Uploads and downloads check size and CRC32 before committing the file.
CSP/RDP has retransmission as an option.

![A file sent and fetched back](docs/media/file-transfer.gif)

### Firmware update

Send, verify, flash, reboot, confirm. MCUboot, FTP for binaries to a specific flash directory or FWU lite for direct downloads. The direct block protocol separates upload from
`fwu flash`.

A optional read only recovery image can be set as well. See the [update guide](docs/fwu/README.md).

![Firmware update over a radio link](docs/media/firmware-update-over-radio.gif)

## Hardware

NUCLEO-L496ZG is the current MCU reference. Test-bench cover
parameters, files, commands, events, CAN, housekeeping, and firmware updates.
See [targets](docs/targets/index.md).

[Project status](docs/status/index.md) records the tested configurations and the TO Do list.

## Targets

| Target | Board | Architecture | Links | What has been tested |
| --- | --- | --- | --- | --- |
| `linux` | `native_sim/native/64` | x86-64 host | KISS over a PTY; optional CAN via SocketCAN | Reference services and optional profiles |
| `nucleo_l496zg` | STM32 Nucleo L496ZG | Arm Cortex-M4 | KISS on USART3; optional CAN on PD0/PD1 | Reference services and optional profiles |
| `frdm_k64f` | NXP FRDM-K64F | Arm Cortex-M4 | OpenSDA UART console | Shell only; CSP, parameters, storage and file transfer are off |
| `rpi_pico_w` | Raspberry Pi Pico W | Arm Cortex-M0+ | USB CDC ACM console | Shell only; CSP, parameters, storage and file transfer are off |

### Identifying a board

The ID is shown by `status` and
`version`, and the ground can read it with `param get <node> hw_id`:

```text
@BOOT sw=v1.0.1 board=nucleo_l496zg/stm32l496xx unit=203037324d46500c0010001f ...
```

![A ground node talking to three boards over CAN and radio](docs/media/multi-board-can.gif)

Here a ground node talks to an STM32 and a Kinetis over CAN, and to an RP2040
over a UHF radio.

## Mission control

[Yamcs](https://yamcs.org/) records housekeeping telemetry. The
[kfsw-yamcs](https://github.com/dgonzalez97/kfsw-yamcs) configuration is a
submodule under `ground-station/yamcs`:

```bash
cd ground-station/yamcs
./mvnw yamcs:run
```

The host bridge pulls CSP parameters and forwards them to Yamcs. Report
definitions generate both the node configuration and the mission database.
Configure housekeeping through K-FSW commands.

The [ground guide](docs/ground/index.md) has a working demo.

## Testing

The pipeline runs:

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
other way, booting a real image and talking to it through a ground node like a real satellite DITL.

[Coverage](https://dgonzalez97.github.io/k-fsw/coverage/)

### Hardware in the loop

[Robot Framework](https://robotframework.org/) drives the physical suites
through [robot-terminal-runner](https://github.com/dgonzalez97/robot-terminal-runner),
a submodule under `tests/platform/`. It sends shell commands through tmux
and records the results in html format to easily identify which command failed.

| Suite | Needs | Covers |
| --- | --- | --- |
| `boot`, `uart` | Nucleo | Boot, reset cause, shell and CSP over the debug UART |
| `param-tables` | Nucleo | Every table present and addressed, with the right write modes |
| `can` | Nucleo + CAN adapter | CSP over CAN: ping, identity and remote settings and fuzzy testing |
| `holybro` | Nucleo + radio pair | CSP, files, commands and events across the link |
| `fwu` | Nucleo + CAN adapter | FTP and FWU lite uploads, slot readbacks, rollback and confirmation, so firmware update is never broken |

Cases needing hardware are tagged, so the same files run in CI without
hardware and on the bench with it. See the [testing guide](docs/testing/index.md).

![Hardware test suite running](docs/media/hardware-test-robot.gif)

And the report it leaves behind:

![Robot report from a physical run](docs/media/hil-robot-report.png)

## Development

Check the [contribution guide](docs/development/index.md).

## License

Licensed under [Apache 2.0](LICENSE). Third-party dependencies retain their
own licences.
