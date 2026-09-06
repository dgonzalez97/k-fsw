# K-FSW - Flight Software for Next Space

[![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-K--FSW-28a96b)](https://dgonzalez97.github.io/k-fsw/)

Open-source flight software built on Zephyr, for small satellites and Cubesats.

It gives a spacecraft what every mission needs before it can do anything
mission-specific: a console, a link to the ground, settings you can read and
change over that link, files, events, logs, commands, housekeeping, and a firmware update mechanism over the air. 


It is written for on-board computers, but can be used for anything in a spacecraft. Because it
is composed rather than hardcoded, the same framework runs on a radio, ADCS, an EPS or
any other board with a processor: you enable what that subsystem needs and leave
the rest out.

[Read the documentation](https://dgonzalez97.github.io/k-fsw/) for setup,
architecture, operations, testing, and the doxygen for the C API.

## Layout of K-FSW


![How the repositories fit together](docs/media/layout.svg)

| Repository | Owns |
| --- | --- |
| [`k-fsw`](https://github.com/dgonzalez97/k-fsw) | Composition, targets, tools, integration tests, docs, its the main app |
| [`kfsw-modules`](https://github.com/dgonzalez97/kfsw-modules) | Device and subsystem modules |
| [`kfsw-services`](https://github.com/dgonzalez97/kfsw-services) | Logging, parameters, persistence, files, events, commands, health, firmware update |
| [`kfsw-comms`](https://github.com/dgonzalez97/kfsw-comms) | CSP, routing, UART/KISS and CAN transports |
| [`kfsw-platform`](https://github.com/dgonzalez97/kfsw-platform) | Zephyr mechanisms: time, storage, reset cause, watchdog, I/O |



## Modules

A **module** only knows about one piece of hardware - a radio, a sensor, a subsystem. It
owns its own interface, its parameters, its shell command and its tests.

Adding one touches nothing shared. A module claims its own block of settings
and hands them to the composition, so no existing file changes and the module
never has to learn about the services.

Examples in  [`kfsw-modules`](https://github.com/dgonzalez97/kfsw-modules):
a UHF radio, and a small worked example using LEDs and buttons of development boards.

## What KFSW does

- **Settings you can change from the ground.** 100 named values in 16 tables,
  each owned by the code it describes, which checks a write before it lands. Can be persisted for reboots, and use a system that makes V&V life easier.
- **CSP.** One router, and a route decides which link a destination takes,
  over [libcsp](https://github.com/libcsp/libcsp).
- **Files** in either direction, checksummed before anything is committed.
- **[Firmware update](#firmware-update) over the air**, with rollback if the
  new image never confirms itself. Two routes: a light block protocol, and one
  over file transfer.
- **Commands, events and health.** A node can be told to do something and
  answer whether it worked, keep a record of what it did while nobody was
  listening, and reset itself when a part of it stops responding.
- **Ground nodes.** The ground segment lives in this same workspace, built from
  the same sources as the flight side, as if it was one extra node on the satelite.
## Using KFSW
The first demo tries to get you to talk between nodes without any hardware at all (
every node here is a Linux process, and they reach each other over
pseudo-terminals exactly as they would over a radio, kiss or CAN )

![Building a node and bringing up a link](docs/media/getting-started.gif)

```bash
west manifest --validate
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

Then, in other terminal:

```bash
./k-fsw/tools/k-ground init                
./k-fsw/tools/k-ground run kfsw-gnd-uhf      
```


`csp ping 16` from the operator node crosses the link and comes back. The
[ground guide](docs/ground/index.md) covers the configuration enviroment and the
other ground roles.


### Settings, across a link

`param tables` lists what a node carries and `param table <id>` prints one.

The settings themselves know nothing about the network. Each one belongs to
the code it describes, which holds it and decides whether a write is allowed;
reaching them from the ground is a separate, optional piece that speaks
[Space Inventor's libparam](https://github.com/spaceinventor/libparam) on the
wire. A node with no radio at all still has every setting, and the code that
owns one never learns that CSP exists.

![Reading and writing parameters across a link](docs/media/param-over-a-link.gif)

### Files

Up, checked on the node, back down, and compared. The same CRC32 appears at every step, which is what makes it a round trip.

![A file sent and fetched back](docs/media/file-transfer.gif)

### Firmware update
send, flash, reboot, confirm. An image that arrives
intact is not the same as one you want to boot, so receiving and committing are
separate steps, and MCUboot puts the old image back if the new one never
confirms itself.
A recovery image, (with minimal modules, flashed in protected memory) can be configured to be the fallback of the A/B images, in the cathastrophic case that 2 main images are failing.

![Firmware update over a radio link](docs/media/firmware-update-over-radio.gif)

## Hardware

The board in use today is an **STM32 Nucleo (L496ZG)**, and several more will
join it before the first release, if they are supported by the Zephyr Proyect. Parameter tables, file transfer, commands, events, CAN and a firmware update have all been exercised on it from a ground node over a real radio link, but also in simulation.

What has and has not been proven on hardware is tracked in the
[project status](docs/status/index.md).

## Targets
 
| Target | Board | Links | What runs on it |
| --- | --- | --- | --- |
| `linux` | `native_sim/native/64` | KISS over a PTY, CAN via SocketCAN | Everything |
| `nucleo_l496zg` | STM32 Nucleo L496ZG | KISS on USART3, CAN on PD0/PD1 | Everything |

They are bring-up profiles, not
flight targets, and build without CSP, parameters, storage or files, to test Zephyr. TBD before release to test them as well.

| Target | Board | Verified scope |
| --- | --- | --- |
| `frdm_k64f` | `frdm_k64f/mk64f12` | OpenSDA UART shell |
| `rpi_pico_w` | `rpi_pico/rp2040/w` | USB CDC ACM shell |

## Testing

Eight jobs run on every push, and all must pass before anything merges:

| Job | What it checks |
| --- | --- |
| `BUILD / linux`, `BUILD / nucleo_l496zg` | Both full targets build, plus a CSP-disabled composition |
| `QUALITY` | clang-format and cppcheck over the sources |
| `UNIT / Twister` | **193 cases** across 17 suites |
| `INTEGRATION / software` | 18 end-to-end smoke scripts driving a real image |
| `MEMORY / Valgrind` | The hosted image under Valgrind |
| `ROBOT / dry-run + software` | Every suite parses; the software-tagged cases run |
| `DOCS / Doxygen` | The documentation builds and the API is documented |

The unit suites cover each layer on its own; the integration robot scripts, booting a real image and talking to it the way an operator would, using a ground node.

### Hardware in the loop

[Robot Framework](https://robotframework.org/) drives the physical suites,
because a HIL run is a sequence of operator actions and Robot is honest about
which ones passed. It works the boards through
[tmux](https://github.com/tmux/tmux), so the same session a person would drive
by hand is the one the suite drives.

| Suite | Needs | Covers |
| --- | --- | --- |
| `boot`, `uart` | Nucleo | Boot markers, reset cause, shell and CSP over the debug UART |
| `param-tables` | Nucleo | Every table present and addressed, with the right write modes |
| `can` | Nucleo + CAN adapter | CSP over CAN: ping, identity and remote settings |
| `holybro` | Nucleo + radio pair | CSP, files, commands and events across the link |
| `fwu` | Nucleo + radio pair | An image sent, flashed and booted |

Cases needing the board are tagged, so the same files run in CI without
hardware and on the bench with it. The [testing guide](docs/testing/index.md)
has the full matrix and how to run them.

The parameter-table suite running against the Nucleo over CAN and RF.

![Hardware test suite running](docs/media/hardware-test-robot.gif)

And the report it leaves behind:

![Robot report from a physical run](docs/media/hil-robot-report.png)

A physical result is only ever recorded when someone watched it happen. The
[project status](docs/status/index.md) separates what exists from what has been
tested in software and what has been read off a board.

## Development


The [documentation site](https://dgonzalez97.github.io/k-fsw/) covers setup,
architecture, operations and the C API, and the same content builds as a
printable guide with 

`./k-fsw/tools/docs/pdf.sh`. 

The [development guide](docs/development/index.md) covers contributions and how to add modules or
changes. 
