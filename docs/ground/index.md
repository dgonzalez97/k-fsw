# Ground Composition {#ground}

K-FSW can be composed as lightweight Linux ground nodes without creating a
second framework. `k-ground` uses the same Zephyr `native_sim` application,
shell commands, libcsp router, KISS interface, services, and build machinery as
KFSW-Linux. Configuration changes node identity and address; it does not fork
service implementations.

```text
                        reusable K-FSW components
                                  |
                     +------------+------------+
                     |                         |
              flight profiles          ground roles
                                               |
                                         local CSP
                                               |
                                      other K-FSW nodes
```

## Identity and node convention

K-FSW currently assigns ground-side addresses starting at 16 by project
convention:

| Role | CSP address | Current scope |
| --- | --- | --- |
| `kfsw-gnd-uhf` | 16 | Own the physical UHF interface and expose it to ground CSP |
| `kfsw-rotctl` | 17 | Reserved configuration for a future antenna-control bridge |
| `kfsw-beacon` | 18 | Reserved configuration for future beacon handling |
| `kfsw-ops` | 19 | Operator-facing shell; does not open the radio |

These names and addresses are configurable deployment choices, not CSP
protocol roles. K-FSW uses CSP v2, whose source and destination fields are 14
bits. The launcher therefore accepts ground nodes in `16..16383`; CSP v1's
familiar 5-bit `0..31` address range does not apply to this composition. A
peer may be any different CSP v2 address in `1..16383`, allowing the UHF
gateway to target a flight-side node such as node 2.

Role, name, hostname, prompt, address, direct peer, optional UHF implementation,
and build directory are generated from configuration rather than hard-coded
into C. `status` makes the selected instance visible, while `uhf status` is
present only when the radio module is composed:

```text
kfsw-gnd-uhf# status
K-FSW status
Role: kfsw-gnd-uhf
Name: kfsw-gnd-uhf
CSP node: 16
board: native_sim/native/64

kfsw-gnd-uhf# uhf status
implementation: holybro-sik
expected serial: 57600 8N1
RF link: unknown
```

Normal Linux images retain the `kfsw:~$` prompt and report `Role: flight`.

## Engineering environment setup

Use three separate environment layers. Keeping them distinct makes failures
easier to locate:

| Layer | Purpose | How it is loaded |
| --- | --- | --- |
| Workspace `.venv` | Python, west, and Zephyr tooling | Activate in the engineer's shell; K-FSW scripts also discover it automatically |
| `ground-station/nodes/*.env` | Version-controlled role, CSP address, and peer | Loaded automatically by `tools/k-ground` |
| Holybro bench environment | Host-specific serial paths and measured radio settings | Explicitly sourced before physical HIL |

From an existing west workspace, prepare a terminal with:

```bash
cd /path/to/k-fsw-workspace
. .venv/bin/activate
west topdir
west manifest --validate
command -v socat
```

`west topdir` should print the workspace root, and `west manifest --validate`
must complete without an error. `socat` is required for local CSP/KISS links.
On Ubuntu, install it with `sudo apt install socat` if the final command prints
nothing.
The complete one-time workspace and host-package procedure is in @ref
getting_started; do not run `west init` again inside an already initialized
workspace.

There is no generic project `.env` that must be executed. Activating `.venv`
configures the development tools; the role files configure K-FSW instances.
Project wrappers source `.venv/bin/activate` when it exists, but activation is
still recommended when invoking `west` directly.

The launcher defaults to the reference deployment in `k-fsw/ground-station`
and generated output in `build/k-ground`. To select a mission deployment and
an explicit build root for the current terminal:

```bash
export KGROUND_STATION_DIR="$PWD/ground-station"
export KGROUND_BUILD_ROOT="$PWD/build/k-ground"
./k-fsw/tools/k-ground build kfsw-gnd-uhf
```

These exports affect only the current shell and its children. Do not add them
globally to `.bashrc` when one host serves more than one mission deployment.
Use `unset KGROUND_STATION_DIR KGROUND_BUILD_ROOT` to return to launcher
defaults.

Inspect the active values before a test with:

```bash
printf 'station=%s\nbuild=%s\n' \
  "$KGROUND_STATION_DIR" "$KGROUND_BUILD_ROOT"
```

Do not place USB device paths in reusable node files. They are properties of a
particular bench host and belong in the separate Holybro bench environment
described below.

## Ground-station configuration

`ground-station/` is a version-controlled deployment description, not a
driver or orchestration framework:

```text
ground-station/
├── README.md
├── station.env
└── nodes/
    ├── kfsw-gnd-uhf.env
    ├── kfsw-rotctl.env
    ├── kfsw-beacon.env
    └── kfsw-ops.env
```

Each node file is a small shell-compatible environment file:

```text
KFSW_ROLE=kfsw-gnd-uhf
KFSW_CSP_NODE=16
KFSW_CSP_PEER=19
KFSW_RADIO_UHF=holybro
```

Only `kfsw-gnd-uhf` selects `KFSW_RADIO_UHF`; ops, beacon, and rotator roles do
not own the physical radio. The launcher maps `holybro` to the reusable module's
compile-time Kconfig choice rather than calling implementation-specific C APIs.

Create a mission-local copy from the west workspace root with:

```bash
./k-fsw/tools/k-ground init
```

The launcher selects `./ground-station` when present. Set
`KGROUND_STATION_DIR` to select another deployment. `init` refuses to replace
an existing directory; it is a copy operation, not an interactive wizard.

## Launching k-ground

Start the configured UHF gateway and operator shell in separate terminals:

```bash
./k-fsw/tools/k-ground run kfsw-gnd-uhf
```

```bash
./k-fsw/tools/k-ground run kfsw-ops
```

For two configured ground peers, `run` connects their native KISS PTYs through
a local Unix socket. This is deliberately a direct two-node demonstration,
not yet a multi-drop ground router. The existing shell syntax then works in
both directions:

```text
kfsw-ops# status
kfsw-ops# csp ping 16
CSP ping 16: success, rtt_ms=...

kfsw-gnd-uhf# csp ping 19
CSP ping 19: success, rtt_ms=...
```

The optional one-command demo presents the operator shell, while the automated
form verifies both identities, prompts, and ping directions:

```bash
./k-fsw/tools/k-ground demo
./k-fsw/tools/k-ground test
```

`kfsw-rotctl` and `kfsw-beacon` use the same build/run path and reserve their
roles without adding unused production protocols. There is no central
orchestration, master election, GUI, database, web service, or new command
framework.

## Physical-interface ownership

One process should own one physical interface. In the prototype,
`kfsw-gnd-uhf` alone opens the Holybro serial device. `kfsw-ops`, the future
rotator bridge, and the future beacon handler communicate through CSP and
remain independent of the radio implementation. A later
`kfsw-gnd-sband` could follow the same pattern without changing those roles.

## UHF and Holybro boundary

The HIL tree models the radio category separately from its implementation:

```text
tests/hil/radio-uhf/
└── holybro/
    ├── raw-peer/
    ├── raw-nucleo-smoke.sh
    └── csp-kiss-smoke.sh
```

`kfsw-comms` continues to own reusable CSP/KISS/UART behavior. `kfsw-modules`
owns the compile-time-selected `radio-uhf` interface and the `holybro-sik`
implementation. That module reports configured identity and expected serial
facts without adding send/receive operations, another UART driver, KISS
framing, CSP interface, or background manager.

No radio parameter definitions are exported yet. The implementation does not
enter SiK command mode or safely apply runtime setting changes, so writable
TX-power, network-ID, or air-rate parameters would accept state without real
hardware behavior. The generic PARAM mechanism remains ready for a future
module-owned definition set once an actual safe operation exists.

The two physical tests answer different questions:

- `raw-nucleo-smoke.sh` flashes a temporary NUCLEO raw peer, sends
  deterministic numbered requests, and requires every matching reply. The
  accepted bench ran 100/100 exchanges with no invalid payload or timeout. It
  proves only that bytes traverse the UART/RF link.
- `csp-kiss-smoke.sh` builds k-ground node 16 and NUCLEO node 2 with 57600-baud
  UART profiles, bridges the ground KISS PTY to the USB radio, and requires
  interfaces, direct routes, bidirectional CSP ping, and clean counters. It
  proves KISS framing and CSP routing in addition to the link.

The recorded USB-side RFD SiK 2.0 settings, exact commands, and latest bench
result are in `tests/hil/radio-uhf/holybro/README.md`. On 30 August 2026, the
corrected bench passed both raw and CSP/KISS acceptance without changing radio
parameters. This is physical functional evidence for that named bench, not RF
or flight qualification.
