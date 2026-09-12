# Ground station {#ground}

[TOC]

`k-ground` runs Linux ground nodes from the same application and services
as KFSW-Linux. Node files set each role's name, address, peer, and build
configuration.

## Identity and node convention

K-FSW currently assigns ground-side addresses starting at 16 by project
convention:

| Role | CSP address | Current scope |
| --- | --- | --- |
| `kfsw-gnd-uhf` | 16 | Own the physical UHF interface and expose it to ground CSP |
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
See @ref getting_started for workspace and host setup.
Do not run `west init` again inside an initialized workspace.

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
    └── kfsw-ops.env
```

Each node file is a small shell-compatible environment file:

```text
KFSW_ROLE=kfsw-gnd-uhf
KFSW_CSP_NODE=16
KFSW_CSP_PEER=19
KFSW_CSP_ROUTES='19/14 KISS'
KFSW_RADIO_UHF=holybro
```

`KFSW_CSP_ROUTES` is optional. When present, `tools/k-ground` validates its
bounded characters/length and writes it to `CONFIG_KFSW_CSP_ROUTE_TABLE` for
that node; the value uses libcsp's comma-separated
`destination[/prefix] interface [via]` syntax. When absent, the one-interface
image retains `0/0 -> KISS direct`. The launcher configures routes but still
only auto-connects the direct two-peer PTY link described below; it is not a
general network orchestrator.

Only `kfsw-gnd-uhf` selects `KFSW_RADIO_UHF`; the ops role does
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
not a multi-drop ground router. The existing shell syntax then works in
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

Addresses 17 and 18 are reserved for future ground roles. They have no
node configuration. The launcher starts configured nodes; the housekeeping
bridge connects them to Yamcs.

## Moving a file between ground nodes

Ground roles are built from the same `native_sim` composition as KFSW-Linux, so
they carry the same file-transfer service. Every path is virtual and sandboxed
below `/kfsw/ftp` on the node that owns it.

From the operator shell, upload a file to the gateway, read its metadata back,
download it again, and compare the two local copies:

```text
kfsw-ops# ftp generate /build/test.txt 256
FTP generate path=/build/test.txt: PASS bytes=256 crc32=0ce9d363
kfsw-ops# ftp 16 mkdir /uplink
FTP mkdir node=16 path=/uplink: PASS
kfsw-ops# ftp put 16 /build/test.txt /uplink/test.txt
FTP put node=16 source=/build/test.txt destination=/uplink/test.txt: PASS bytes=256 crc32=0ce9d363 ...
kfsw-ops# ftp stat 16 /uplink/test.txt
FTP stat node=16 path=/uplink/test.txt type=file bytes=256 crc32=0ce9d363
kfsw-ops# ftp get 16 /uplink/test.txt /build/test-returned.txt
FTP get node=16 source=/uplink/test.txt destination=/build/test-returned.txt: PASS bytes=256 ...
kfsw-ops# ftp verify /build/test.txt /build/test-returned.txt
FTP verify first=/build/test.txt second=/build/test-returned.txt: PASS
```

`ftp generate` writes a deterministic byte pattern rather than prose; the
transfer service treats every file as binary. The same CRC appearing on the
sender, on the receiver's `stat`, and on the returned copy is the end-to-end
evidence that the round trip was exact.

The gateway can inspect what it received without opening a connection, because
a request addressed to a node's own CSP address is served from local storage:

```text
kfsw-gnd-uhf# ftp 16 ls /uplink
f        256 test.txt
FTP list: PASS entries=1
```

`tests/k-ground-ftp-smoke.sh` runs exactly this sequence between node 19 and
node 16, including the missing-file negative path, and is part of the software
integration suite. Transfers to and from a flight node over the radio are a
separate physical path; see the Holybro fixture under `tests/hil/radio-uhf/`.

## Telemetry in Yamcs

[Yamcs](https://yamcs.org/) records housekeeping samples and provides the
telemetry browser and archive. The project configuration lives in the
`ground-station/yamcs` submodule.

```bash
cd k-fsw/ground-station/yamcs && ./mvnw yamcs:run    # then http://localhost:8090
```

By default nothing arrives on its own: housekeeping answers when asked, so a
bridge does the asking.

```bash
./k-fsw/tools/ground/hk-bridge.py --device /dev/pts/7 --node 1 --report 0
```

It speaks CSP over KISS on the host, pulls samples, and forwards each one to
Yamcs on UDP 10015. Point it at a hosted node's `uart_1` pseudo-terminal, or
straight at the Holybro's serial device for a flight node over the radio; it is
the same client either way.

### Beacons

A report can send its latest sample periodically without a ground request:

```text
hk beacon 0 1 5000      # report 0, to node 1, every five seconds
hk beacon 0 1 0         # stop
```

The ground does not change. A beacon is the same frame a request gets, sent
from the same port, so the bridge recognises it with what it already has — it
only has to stop asking:

```bash
./k-fsw/tools/ground/hk-bridge.py --device /dev/pts/7 --node 1 --listen
```

Beacon limits:

- Intervals below `CONFIG_KFSW_HK_BEACON_FLOOR_MS` (5000 ms by default)
  return `-ERANGE`.
- A report must be collecting and the node's clock must be set.
- A send is skipped below `CONFIG_KFSW_HK_BEACON_BUFFER_RESERVE` free
  CSP buffers. Check `hk show` and `hk_beacons_skipped` in table 33.

Beacons use a separate destination port to avoid service request handlers.
Beacon intervals reset at boot; report definitions and collection periods persist.

### Bringing it up and checking it

Yamcs needs Java 17 and nothing else; Maven downloads itself through `./mvnw`.

```bash
cd k-fsw/ground-station/yamcs
./mvnw yamcs:run
```

It prints `Yamcs started` after a few seconds and serves <http://localhost:8090>.
The instance is `kfsw`. Leave it running in its own terminal — it holds the
archive, so stopping it stops the recording.

**Check the mission database before connecting hardware.** In another
terminal, from `k-fsw/ground-station/yamcs`:

```bash
./scripts/check-mdb.sh
```

That pushes a housekeeping frame captured off a real node into the running
instance and reads the values back out. It is what CI runs on every push, and
it is the fastest way to tell whether a change to a report definition broke the
decoding. Expect `MDB CHECK RESULT: PASS`.

**Then bring in a node.** Define the report on the node and let it collect:

```text
kfsw:~$ hk define 0 51:0x00 51:0x10 51:0x08 3:0x00 3:0x0c
kfsw:~$ csp clock set 1789066008
kfsw:~$ hk period 0 2000
kfsw:~$ hk show
```

`hk show` is the node's own account of itself: how many collections, how many
failed, how many values were absent, and how many samples the ring is holding.
Set the clock first or every sample carries a zero timestamp and the bridge
falls back to host time.

Then start the bridge against the node's link:

```bash
./k-fsw/tools/ground/hk-bridge.py --device "$KGROUND_HOLYBRO_DEVICE" \
    --baud 57600 --node 2 --report 0 --count 8 --interval 10
```

The bridge prints each forwarded sample. Use `--yamcs none` to inspect
received frames as hex before checking Yamcs.

**What to look at in the web interface.**

| Where | What it tells you |
| --- | --- |
| Links | `hk-udp` should show `OK, receiving on 10015`, with valid datagrams climbing and invalid at zero |
| Telemetry, Packets | one packet per sample, in the `nucleo_temperature` container |
| Telemetry, Parameters | `/kfsw/nucleo_temperature_temp_mcu` in degrees, with `ACQUIRED` beside it |
| Archive | the history — this is the part a console cannot give you |

A parameter marked `INVALID` rather than `ACQUIRED` is the mission database
doing its job: the temperature carries a valid range, so a node reporting the
reserved value shows as absent instead of plotting at minus two million
degrees. Check `temp_valid` and `temp_failures` beside it.

The same answers are available without the browser, which is what a script
should use:

```bash
curl -s localhost:8090/api/links/kfsw/hk-udp | python3 -m json.tool
curl -s localhost:8090/api/processors/kfsw/realtime/parameters/kfsw/nucleo_temperature_temp_mcu
curl -s 'localhost:8090/api/archive/kfsw/parameters/kfsw/nucleo_temperature_temp_mcu?limit=50&order=asc'
```

**If nothing arrives:** check frames with `--yamcs none`, check that
`hk show` reports increasing collections, then check Yamcs Links for invalid
datagrams. Verify the serial path, report definition, and UDP link settings.

### Yamcs reads; K-FSW commands

Yamcs records telemetry. Configure housekeeping through K-FSW commands:
`hk_define`, `hk_period`, and `hk_clear` reach nodes over CSP/KISS or CAN.

```text
kfsw-ops# cmd 2 hk_define 0 "51:0x00 51:0x10 3:0x00"
hk_define node=2: OK report 0 defines 3 values
```

Yamcs telecommand integration is not implemented.

### Report definitions

Housekeeping frames carry values in report order, without names.
`hk-report.py` generates the node definition and Yamcs XTCE database from
one YAML file. The bridge forwards the frame unchanged.

```bash
report=k-fsw/ground-station/reports/nucleo-temperature.yaml
./k-fsw/tools/ground/hk-report.py "$report" define
# hk define 0 51:0x00 51:0x10 51:0x08 3:0x00 3:0x0c

./k-fsw/tools/ground/hk-report.py "$report" xtce \
    -o k-fsw/ground-station/yamcs/src/main/yamcs/mdb/kfsw-hk.xml
```

`hk-report.py check` compares the file against a node's own `param list`, so a
parameter that moves offset is caught rather than silently shifting every value
after it.

### The bridge's envelope

Each datagram is twelve bytes the bridge adds, then the frame the node sent:

```text
 0  u64  Unix milliseconds   the node's clock, or the host's when it is unset
 8  u32  sequence
12  ...  the housekeeping frame, header and all
```

The envelope adapts the node's timestamp and sequence to Yamcs field sizes,
preserving each sample's generation time during batch retrieval.

`tests/hk-yamcs-smoke.sh` asserts the thing that matters: the bytes the bridge
pulls off the link are the bytes the node's own shell prints for the same
sample. It is also the only test of housekeeping's CSP server, which the unit
suites do not reach.

## Physical-interface ownership

One process should own one physical interface. In the prototype,
`kfsw-gnd-uhf` alone opens the Holybro serial device. `kfsw-ops` and a future
rotator bridge would communicate through CSP and remain independent of the
radio implementation. A later
`kfsw-gnd-sband` could follow the same pattern without changing those roles.

## UHF and Holybro setup

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
result are in `tests/hil/radio-uhf/holybro/README.md`. The
recorded bench passed both raw and CSP/KISS acceptance without changing radio
parameters. This is physical functional evidence for that named bench, not RF
or flight qualification.
