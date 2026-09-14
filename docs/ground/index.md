# Ground station {#ground}

[TOC]

`k-ground` runs Linux ground nodes built from the same application and
services as KFSW-Linux. A node file sets each role's name, address, peer and
build options.

## Roles and addresses

| Role | CSP address | Use |
| --- | --- | --- |
| `kfsw-gnd-uhf` | 16 | Opens the UHF radio for the ground network |
| `kfsw-gnd-uhf-bench` | 16 | The same, routed to flight node 2 on the radio bench |
| `kfsw-gnd-can` | 16 | Reaches a flight node over a SocketCAN interface |
| `kfsw-ops` | 19 | Operator shell; doesn't open the radio |

These are the reference settings. CSP v2 addresses are 14 bits, so the
launcher accepts ground nodes from 16 to 16383 and peers from 1 to 16383.

The role, name, prompt, address, peer, radio and build directory come from the
node file. `status` shows them, and `uhf status` is there when the radio module
is built in:

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
    |-- kfsw-gnd-uhf-bench.env
    |-- kfsw-gnd-uhf.env
    `-- kfsw-ops.env
```

A node file is a shell environment file:

```text
KFSW_ROLE=kfsw-gnd-uhf
KFSW_CSP_NODE=16
KFSW_CSP_PEER=19
KFSW_RADIO_UHF=holybro
```

`KFSW_CSP_ROUTES` sets a route table, for example `'2/14 KISS'`.
`tools/k-ground` checks it and writes it to `CONFIG_KFSW_CSP_ROUTE_TABLE`;
without it the node uses `0/0 -> KISS direct`. `KFSW_EXTRA_KCONFIG` and
`KFSW_EXTRA_OVERLAY` add Kconfig lines and a devicetree overlay, which is how
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
CSP ping 16: success, rtt_ms=...

kfsw-gnd-uhf# csp ping 19
CSP ping 19: success, rtt_ms=...
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

The same CRC on both nodes and on the returned copy means the file came back
unchanged. The gateway can list its own files without a connection:

```text
kfsw-gnd-uhf# ftp 16 ls /uplink
f        256 test.txt
FTP list: PASS entries=1
```

`tests/k-ground-ftp-smoke.sh` runs this sequence, including a missing file.

## Telemetry in Yamcs

[Yamcs](https://yamcs.org/) stores housekeeping samples and has a telemetry
browser and archive. Its configuration is the `ground-station/yamcs`
submodule.

```bash
cd k-fsw/ground-station/yamcs && ./mvnw yamcs:run    # http://localhost:8090
```

Nodes answer housekeeping requests, so the bridge polls them:

```bash
./k-fsw/tools/ground/hk-bridge.py --device /dev/pts/7 --node 1 --report 0
```

The bridge talks CSP over KISS, pulls samples and sends each one to Yamcs on
UDP port 10015. Point it at a Linux node's `uart_1` PTY, or at the Holybro
serial device to reach a flight node over the radio.

### Beacons

A report can also send its latest sample periodically:

```text
hk beacon 0 1 5000      # report 0, to node 1, every five seconds
hk beacon 0 1 0         # stop
```

A beacon is the same frame a request gets, sent from the same port, so the
bridge only has to stop polling:

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

Yamcs only records telemetry. Housekeeping is configured with K-FSW commands;
`hk_define`, `hk_period` and `hk_clear` work over CSP/KISS or CAN:

```text
kfsw-ops# cmd 2 hk_define 0 "51:0x00 51:0x10 3:0x00"
hk_define node=2: OK report 0 defines 3 values
```

### Report definitions

Housekeeping frames carry the values in report order, without names.
`hk-report.py` generates the node command and the Yamcs XTCE database from one
YAML file:

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

Yamcs reads the time and sequence with these sizes, so each sample keeps its
own time when several are pulled at once.

`tests/hk-yamcs-smoke.sh` checks that the bytes the bridge receives match what
the node's shell prints for the same sample.

## Radio

Only one process opens each physical interface. `kfsw-gnd-uhf` opens the
Holybro serial device, and the other ground roles reach it over CSP.

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
