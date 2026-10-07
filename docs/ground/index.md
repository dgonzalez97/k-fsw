# Ground station {#ground}

[TOC]

`k-ground` runs Linux ground nodes built from the same application and
services as KFSW-Linux. A node file sets each role's name, address, peer and
build options.

## Roles and addresses

| Role | CSP address | Use |
| --- | --- | --- |
| `kfsw-gnd-uhf` | 16 | Opens the UHF radio for the ground network |
| `kfsw-gnd-can` | 16 | Reaches a flight node over a SocketCAN interface |
| `kfsw-ops` | 19 | Operator shell; doesn't open the radio |

These are the reference settings. Each node file sets two addresses:

- `KFSW_CSP_NODE` is the node's own CSP address. Flight nodes use 1 to 15 and
  ground nodes start at 16, so the two never collide; `tools/k-ground`
  refuses a ground node below 16. It accepts up to 16382 under CSP 2 and up
  to 30 under CSP 1.
- `KFSW_CSP_VERSION=1` builds the node for CSP 1, to talk to a CSP 1
  satellite; it is unset or 2 otherwise. A CSP 1 node gets its own
  configuration file and build directory, with a `-csp1` suffix.
- `KFSW_CSP_PEER` is the node at the other end of the serial link, the one
  `comms uart test` pings when given no node. It is not a route: where packets go is
  the route table, `KFSW_CSP_ROUTES`.

A bench usually needs its own variant of a role, for example the UHF gateway
routed to flight node 2. Copy the role to a file ending in `-bench.env`, such
as `kfsw-gnd-uhf-bench.env`; git ignores those, so device paths and bench
routes stay on the machine they belong to.

The node file sets the role, name, prompt, address, peer, radio and build
directory. Use `status` for node identity and `comms uhf status` for radio settings:

```text
kfsw-gnd-uhf# status
K-FSW status
Role: kfsw-gnd-uhf
Name: kfsw-gnd-uhf
CSP node: 16
board: native_sim/native/64
```

A normal Linux image keeps the `kfsw:~$` prompt and `Role: flight`.

## Environment

| Layer | Contents | Loaded by |
| --- | --- | --- |
| Workspace `.venv` | Python, west and Zephyr tools | Your shell; K-FSW scripts activate it themselves |
| `ground-station/nodes/*.env` | Role, CSP address, peer and routes | `tools/k-ground` |
| Holybro bench file | Serial paths and radio settings of one host | Sourced before a hardware test |

From the workspace root:

```bash
. .venv/bin/activate
west topdir
west manifest --validate
command -v socat
```

`socat` is needed for the local CSP/KISS links (`sudo apt install socat`).
See @ref getting_started for the workspace setup.

The launcher uses `k-fsw/ground-station` and writes to `build/k-ground` by
default. To use other directories in the current shell:

```bash
export KGROUND_STATION_DIR="$PWD/ground-station"
export KGROUND_BUILD_ROOT="$PWD/build/k-ground"
./k-fsw/tools/k-ground build kfsw-gnd-uhf
```

Keep USB device paths out of the node files and put them in the bench file.

## Configuration

```text
ground-station/
|-- README.md
|-- station.env
|-- reports/
`-- nodes/
    |-- kfsw-gnd-can.env
    |-- kfsw-gnd-uhf.env
    `-- kfsw-ops.env
```

A node file is a shell environment file; see
[kfsw-gnd-uhf.env](https://github.com/dgonzalez97/k-fsw/blob/main/ground-station/nodes/kfsw-gnd-uhf.env)
for the gateway and
[kfsw-gnd-can.env](https://github.com/dgonzalez97/k-fsw/blob/main/ground-station/nodes/kfsw-gnd-can.env)
for one that adds Kconfig and an overlay.

`KFSW_CSP_ROUTES` sets a route table, for example `'2 KISS'`, which means
node 2 in either CSP version.
`tools/k-ground` checks it and writes it to `CONFIG_KFSW_CSP_ROUTE_TABLE`;
without it the node keeps `0/0 KISS` from `config/profiles/k-ground.conf`.
`KFSW_EXTRA_KCONFIG` and `KFSW_EXTRA_OVERLAY` add Kconfig lines and a devicetree overlay, which is how
`kfsw-gnd-can` enables CAN. `KFSW_RADIO_UHF=holybro` selects the radio module.

To copy the reference configuration into your workspace:

```bash
./k-fsw/tools/k-ground init
```

The launcher uses `./ground-station` when it exists, or `KGROUND_STATION_DIR`.
`init` doesn't overwrite an existing directory.

## Running

Start the UHF gateway and the operator shell in two terminals:

```bash
./k-fsw/tools/k-ground run kfsw-gnd-uhf
```

```bash
./k-fsw/tools/k-ground run kfsw-ops
```

`run` connects the KISS PTYs of the two peers through a local socket:

```text
kfsw-ops# csp ping 16
CSP ping 16: success
rtt_ms: ...

kfsw-gnd-uhf# csp ping 19
CSP ping 19: success
rtt_ms: ...
```

`k-ground demo` starts both and opens the operator shell. `k-ground test`
checks both identities, prompts and ping directions:

```bash
./k-fsw/tools/k-ground demo
./k-fsw/tools/k-ground test
```

## Moving a file between ground nodes

Ground nodes include the file transfer service. From the operator shell:

```text
kfsw-ops# ftp generate /build/test.txt 256
FTP generate /build/test.txt: PASS
bytes: 256
crc32: 0ce9d363
kfsw-ops# ftp mkdir 16 /uplink
FTP mkdir 16 /uplink: PASS
kfsw-ops# ftp put 16 /build/test.txt /uplink/test.txt
FTP put 16 /build/test.txt -> /uplink/test.txt: PASS
bytes: 256
crc32: 0ce9d363
duration_ms: 190
throughput_Bps: 1347
kfsw-ops# ftp stat 16 /uplink/test.txt
FTP stat 16 /uplink/test.txt
type: file
bytes: 256
crc32: 0ce9d363
kfsw-ops# ftp get 16 /uplink/test.txt /build/test-returned.txt
FTP get 16 /uplink/test.txt -> /build/test-returned.txt: PASS
bytes: 256
crc32: 0ce9d363
...
kfsw-ops# ftp verify /build/test.txt /build/test-returned.txt
FTP verify /build/test.txt /build/test-returned.txt: PASS
```

The same CRC on both nodes and on the returned copy means the file came back
unchanged. The gateway can list its own files without a connection:

```text
kfsw-gnd-uhf# ftp ls 16 /uplink
FTP ls 16 /uplink
file        256 test.txt
entries: 1
```

`tests/k-ground-ftp-smoke.sh` runs this sequence, including a missing file.

## Telemetry in Yamcs

[Yamcs](https://yamcs.org/) stores housekeeping samples and has a telemetry
browser and archive. Its configuration is the `ground-station/yamcs`
submodule.

Nodes answer housekeeping requests, so the bridge polls them:

```bash
./k-fsw/tools/ground/hk-bridge.py --device /dev/pts/7 --node 1 --report 0
```

The bridge talks CSP over KISS, pulls samples and sends each one to Yamcs on
UDP port 10015. Point it at a Linux node's `uart_1` PTY, or at the Holybro
serial device to reach a flight node over the radio. It speaks CSP 2; add
`--csp-version 1` for a node built with `config/profiles/csp-v1.conf`.

### Beacons

A report can also send its latest sample periodically:

```text
hk beacon 0 1 5000      # report 0, to node 1, every five seconds
hk beacon 0 1 0         # stop
```

Beacons use the same frame format as polled samples. Receive them without
polling:

```bash
./k-fsw/tools/ground/hk-bridge.py --device /dev/pts/7 --node 1 --listen
```

- Intervals below `CONFIG_KFSW_HK_BEACON_FLOOR_MS` (5000 ms) return `-ERANGE`.
- The report has to be collecting and the node's clock has to be set.
- A beacon is skipped when fewer than `CONFIG_KFSW_HK_BEACON_BUFFER_RESERVE`
  CSP buffers are free; check `hk show` and `hk_beacons_skipped` in table 33.

Beacons are sent to their own port (`KFSW_HK_BEACON_PORT`) so they never reach
a request handler. With persistence enabled, beacon settings are saved with
the report.

### Running Yamcs

Yamcs needs Java 17; Maven comes through `./mvnw`.

```bash
cd k-fsw/ground-station/yamcs
./mvnw yamcs:run
```

It prints `Yamcs started` and serves <http://localhost:8090>. The instance is
`kfsw`. Keep it running while recording.

Check the mission database before connecting hardware. In another terminal,
from `k-fsw/ground-station/yamcs`:

```bash
./scripts/check-mdb.sh
```

It sends a recorded housekeeping frame to Yamcs and reads the values back; CI
runs it on every push. Expect `MDB CHECK RESULT: PASS`.

Then set up a report on the node:

```text
kfsw:~$ hk define 0 51:0x00 51:0x10 51:0x08 3:0x00 3:0x0c
kfsw:~$ csp clock set 1789066008
kfsw:~$ hk period 0 2000
kfsw:~$ hk show
```

`hk show` prints the collections, failures, missing values and stored
samples. Set the clock first, otherwise samples have a zero timestamp and the
bridge uses the host time.

Start the bridge on the node's link:

```bash
./k-fsw/tools/ground/hk-bridge.py --device "$KGROUND_HOLYBRO_DEVICE" \
    --baud 57600 --node 2 --report 0 --count 8 --interval 10
```

The bridge prints each sample. With `--yamcs none` it prints the frames as hex
and sends nothing.

| Yamcs page | What to check |
| --- | --- |
| Links | `hk-udp` shows `OK, receiving on 10015`; valid datagrams go up, invalid stays at zero |
| Telemetry, Packets | One packet per sample in the `nucleo_temperature` container |
| Telemetry, Parameters | `/kfsw/nucleo_temperature_temp_mcu` in degrees, marked `ACQUIRED` |
| Archive | The stored history |

`INVALID` instead of `ACQUIRED` means the value is outside its valid range, for
example the reserved value sent when the sensor can't be read. Check
`temp_valid` and `temp_failures`.

The same data is available from the Yamcs API:

```bash
curl -s localhost:8090/api/links/kfsw/hk-udp | python3 -m json.tool
curl -s localhost:8090/api/processors/kfsw/realtime/parameters/kfsw/nucleo_temperature_temp_mcu
curl -s 'localhost:8090/api/archive/kfsw/parameters/kfsw/nucleo_temperature_temp_mcu?limit=50&order=asc'
```

If nothing arrives, look at the frames with `--yamcs none`, check that
`hk show` counts collections, and then check Links for invalid datagrams.

### Commanding

Yamcs only records telemetry. Housekeeping is configured from a ground shell
with the node first; `hk define`, `hk period` and `hk clear` work over CSP/KISS
or CAN:

```text
kfsw-ops# hk define 2 0 51:0x00 51:0x10 3:0x00
node: 2
report 0 defines 3 values
```

### Report definitions

Housekeeping frames carry the values in report order, without names.
`hk-report.py` generates the node command and the Yamcs XTCE database from one
YAML file. Put the node number after `hk define` to send it from the ground:

```bash
report=k-fsw/ground-station/reports/nucleo-temperature.yaml
./k-fsw/tools/ground/hk-report.py "$report" define
# hk define 0 51:0x00 51:0x10 51:0x08 3:0x00 3:0x0c

./k-fsw/tools/ground/hk-report.py "$report" xtce \
    -o k-fsw/ground-station/yamcs/src/main/yamcs/mdb/kfsw-hk.xml
```

`hk-report.py check` compares the file with a node's `param list`, which
catches a parameter that moved.

### Bridge datagrams

Each UDP datagram has a 12-byte header followed by the frame from the node:

```text
 0  u64  Unix milliseconds   the node's clock, or the host's when it is not set
 8  u32  sequence
12  ...  housekeeping frame
```

The bridge sends one datagram per sample, preserving its timestamp.

`tests/hk-yamcs-smoke.sh` checks that the bytes the bridge receives match what
the node's shell prints for the same sample.

## Radio

Only one process opens each physical interface. A serial bridge connects the
`kfsw-gnd-uhf` KISS PTY to the Holybro device; other ground roles reach the
gateway over CSP. See the Holybro fixture below for the bridge command.

```text
tests/hil/radio-uhf/
`-- holybro/
    |-- raw-peer/
    |-- raw-nucleo-smoke.sh
    `-- csp-kiss-smoke.sh
```

`kfsw-comms` has the UART, KISS and CSP code. `kfsw-modules` has the
`radio-uhf` module and its `holybro-sik` implementation, which reports the
expected radio settings and adds encryption. Table 50 has the radio identity,
status and encryption settings. TX power, network ID and air rate are not
writable because the module doesn't use SiK command mode.

- `raw-nucleo-smoke.sh` flashes a raw test peer on the NUCLEO and exchanges
  numbered messages over the radio.
- `csp-kiss-smoke.sh` builds ground node 16 and NUCLEO node 2 at 57600 baud,
  bridges the ground PTY to the USB radio, and checks the interfaces, routes,
  ping in both directions and the counters.

Radio settings and commands are in `tests/hil/radio-uhf/holybro/README.md`.

## Saving and replaying HK

From `k-fsw`, capture accepted HK samples to a new JSONL file:

```bash
tools/kfsw-linux hk --device /dev/pts/7 --node 1 --listen --capture hk.jsonl --yamcs none
tools/kfsw-linux hk --replay hk.jsonl --yamcs 127.0.0.1:10015
```

Each version-1 record contains the source node, host receipt time in milliseconds,
and original HK bytes. Replay opens no serial device and sends the saved samples
in file order without delays. The Yamcs envelope uses the original sample time,
or the saved receipt time when the node's clock was unset. An invalid or
truncated record stops replay; earlier records may already have been sent.

Distinct reports with the same sequence number are kept. Exact duplicate
samples from the same node are suppressed for up to 60 seconds, within a
1024-entry window. A changed timestamp or value makes a sample distinct. There
is no boot identifier on the wire, so a byte-identical sample after a reset
cannot be distinguished until it leaves that window. Captures contain samples
accepted after this filtering, not every raw frame on the link.

Polling counts only matching replies towards `--count`; unrelated beacons can
still be captured. An incomplete `--once` poll exits unsuccessfully and retains
any accepted samples. Continuous polling reports the timeout and retries after
`--interval`. Passive listening stays active between receive windows.

The bridge checks framing, CRCs, HK version, flags and size bounds. It cannot
verify each value's width without the report definition; keep the matching
report YAML with a capture. JSONL files grow with accepted samples, so stop or
rotate captures between runs. Live serial access needs PySerial; offline replay
uses only Python's standard library.
