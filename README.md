# K-FSW — flight software for NewSpace

[![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-K--FSW-28a96b)](https://dgonzalez97.github.io/k-fsw/)

Open-source flight software built on Zephyr, for small satellites.

It gives a spacecraft what every mission needs before it can do anything
mission-specific: a console, a link to the ground, settings you can read and
change over that link, files, events, commands, a watchdog, and a way to
replace the running image.

It is written for on-board computers, but nothing in it assumes one. Because it
is composed rather than forked, the same framework runs on a radio, an EPS or
any other board with a processor: you enable what that board needs and leave
the rest out.

[Read the documentation](https://dgonzalez97.github.io/k-fsw/) for setup,
architecture, operations, testing, and the C API.

## Layout

Five repositories. `k-fsw` composes the application and owns the targets,
tools, integration tests and documentation; the other four hold everything
reusable, pinned by [`west.yml`](west.yml).

```text
                         k-fsw
        application composition, targets, tests, tools, docs
                            |
       +-------------+-------------+-------------+-------------+
       |             |             |             |
kfsw-modules   kfsw-services   kfsw-comms   kfsw-platform
  hardware:     log, param,     CSP, routing,  time, storage,
  radio, I/O    files, events,  UART/KISS,     reset cause,
                commands,       CAN            watchdog
                health, update
       |             |             |             |
       +-------------+-------------+-------------+
                            |
                          Zephyr
                kernel, devices, drivers, build
                            |
          +-----------------+------------------+
          |                 |                  |
       Linux         STM32 Nucleo board   shell bring-ups
    native_sim           L496ZG            FRDM / Pico W
```

Dependencies run one way, down. A layer never includes a header from a layer
above it — which is why the core parameter tables live in the composition
rather than in `platform` or `comms`: those sit *below* the parameter service,
so they cannot reach up to it.

| Repository | Owns | CI |
| --- | --- | --- |
| [`k-fsw`](https://github.com/dgonzalez97/k-fsw) | Composition, targets, tools, integration tests, docs | [![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml) |
| [`kfsw-modules`](https://github.com/dgonzalez97/kfsw-modules) | Device and subsystem modules: `radio-uhf`, `hw_test` | via `k-fsw` |
| [`kfsw-services`](https://github.com/dgonzalez97/kfsw-services) | Logging, parameters, persistence, files, events, commands, health, firmware update | via `k-fsw` |
| [`kfsw-comms`](https://github.com/dgonzalez97/kfsw-comms) | CSP lifecycle, routing, UART/KISS and CAN transports | via `k-fsw` |
| [`kfsw-platform`](https://github.com/dgonzalez97/kfsw-platform) | Zephyr-facing mechanisms: time, storage, reset cause, watchdog | via `k-fsw` |

The dependencies have no CI of their own, deliberately. A commit in one of them
means nothing until a composition pins it, so `k-fsw` builds and tests the
exact set of revisions that make one working system.

## Modules

The framework is meant to be composed, not forked. A **module** is the code
that knows about one piece of hardware — a radio, a sensor, a subsystem. It
owns its own interface, its settings, its shell command and its tests.

What it does *not* own is the plumbing it uses to get there. The UHF radio
moves bytes over a serial link without taking UART, KISS, CSP or routing with
it; those belong to the layers below and stay generic.

Adding one touches nothing shared. A module claims its own block of settings
and hands them to the composition, so no existing file changes and the module
never has to learn that a network exists.

Two ship today, both in
[`kfsw-modules`](https://github.com/dgonzalez97/kfsw-modules): the UHF radio,
and `hw_test` — a deliberately small worked example of the whole boundary,
readable in one sitting.

## What it does

- **Settings you can change from the ground.** 100 named values in 16 tables,
  each owned by the code it describes, which checks a write before it lands.
- **CSP over a radio or over CAN.** One router, and a route decides which link
  a destination takes.
- **Files** in either direction, checksummed before anything is committed.
- **Firmware update** over the air, with rollback if the new image never
  confirms itself.
- **Commands, events and health.** Typed calls with typed results, a bounded
  record of what the node did, and a watchdog fed by a policy rather than a
  timer.
- **Ground nodes.** `k-ground` builds the other end of the link as a Linux
  process, so two nodes need one board.

## Hardware

The full composition runs in native simulation and on an
**STM32 Nucleo board (L496ZG)**. Parameter tables, file transfer, commands,
events, CAN and a firmware update have all been exercised on that board from a
ground node over a real radio link — not only in simulation.

Firmware update is the one worth showing, because it is the whole chain in one
go: send, flash, reboot, confirm.

![Firmware update over a radio link](docs/media/firmware-update-over-radio.gif)

An image that arrives intact is not the same as one you want to boot, so
receiving and committing are separate steps, and MCUboot puts the old image
back if the new one never confirms itself. A bad upload costs a reboot, not the
spacecraft.

What has and has not been proven on hardware is tracked in the
[project status](docs/status/index.md).

## Try it

From a configured west workspace root:

```bash
west manifest --validate
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

That gives you a shell:

```text
kfsw:~$ status
kfsw:~$ param tables
kfsw:~$ param table 1
kfsw:~$ storage info
```

`param tables` lists what this node carries, and `param table <id>` prints one
of them. Add a node number to either — `param table 2 1` — to read the same
thing from across a link.

To bring up the other end, in two more terminals:

```bash
./k-fsw/tools/k-ground init
./k-fsw/tools/k-ground run kfsw-gnd-uhf
```

```bash
./k-fsw/tools/k-ground run kfsw-ops
```

`csp ping 16` from the operator node checks the local link. The
[ground guide](docs/ground/index.md) covers the configuration model, the other
reserved roles, and where the radio bench begins.

## Targets

| Target | Board | Links | What runs on it |
| --- | --- | --- | --- |
| `linux` | `native_sim/native/64` | KISS over a PTY, CAN via SocketCAN | Everything |
| `nucleo_l496zg` | STM32 Nucleo L496ZG | KISS on USART3, CAN on PD0/PD1 | Everything |

Two more boards run a shell and nothing else. They are bring-up profiles, not
flight targets, and build without CSP, parameters, storage or files:

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

The unit suites cover each layer on its own. The integration scripts go the
other way: they boot a real image and talk to it the way an operator would.

### Hardware in the loop

Robot Framework drives the physical suites, because a HIL run is a sequence of
operator actions and Robot is honest about which ones passed.

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

The parameter-table suite running against the Nucleo:

![Hardware test suite running](docs/media/hardware-test-robot.gif)

And the report it leaves behind:

![Robot report from a physical run](docs/media/hil-robot-report.png)

A physical result is only ever recorded when someone watched it happen. The
[project status](docs/status/index.md) separates what exists from what has been
tested in software and what has been read off a board.

## Development

Run the full software gate from the workspace root before opening anything:

```bash
./k-fsw/tools/ci/all.sh
```

The [documentation site](https://dgonzalez97.github.io/k-fsw/) covers setup,
architecture, operations and the C API, and the same content builds as a
printable guide with `./k-fsw/tools/docs/pdf.sh`. The
[development guide](docs/development/index.md) covers contributions and how a
change spanning several pinned repositories is landed.
