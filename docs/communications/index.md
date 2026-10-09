# CSP and links {#communications}

[TOC]

## CSP in K-FSW

[CubeSat Space Protocol](https://github.com/libcsp/libcsp) provides node
addresses, ports, routing and packet transport. `kfsw-comms` sets up libcsp,
the interfaces, the routes and the router.

CSP is optional. With `CONFIG_KFSW_CSP=n`, local parameters, persistence,
logging and the shell still work; remote parameters, FTP and the other network
services need it.

## Terms

**Node.** One CSP endpoint: a flight computer, a subsystem, a process or a
ground tool. Its address is set by `CONFIG_KFSW_CSP_ADDRESS`. KFSW-Linux is
node 1 by default and the NUCLEO-L496ZG node 2. Every node on a network needs
its own address.

**Protocol version.** K-FSW uses CSP 2 by default: a six-byte header and
14-bit addresses, so nodes 1 to 16382, with 16383 as broadcast. Adding
`config/profiles/csp-v1.conf` (`CONFIG_KFSW_CSP_VERSION_1=y`) selects CSP 1
for talking to older CSP 1 systems: a four-byte header and 5-bit addresses,
so nodes 1 to 30, with 31 as broadcast. Every node on a link must use the same
version; a CSP 2 node and a CSP 1 node do not understand each other. An
address the selected version cannot carry stops the build. `csp info` prints
the version in use as `protocol: CSP vN`.

CSP 1 is less exercised than CSP 2. What the tests cover: a two-node exchange,
remote PARAM, multi-KISS, and firmware upload through FWU lite and FTP over
clean and lossy pseudo-terminal links, plus NUCLEO builds with and without
CAN. What they do not: physical CAN, RF, bootloader swaps, rollback and
confirmation. And this is libcsp 2.x in its CSP 1 mode, so byte compatibility
with a real libcsp 1.x node is untested.

**Port.** A service on a node: "node 2, port 9" is the file transfer service on
node 2.

| Port | Service | Option |
| --- | --- | --- |
| 0 | CSP management (CMP), which answers `csp ident` and `csp ifstat` | libcsp |
| 1 | Ping | libcsp |
| 8 | Remote shell execution | `KFSW_REMEXEC_CSP_PORT` |
| 9 | File transfer | `KFSW_FTP_CSP_PORT` |
| 10 | libparam values | `KFSW_PARAM_PORT` |
| 11 | Commands | `KFSW_COMMAND_CSP_PORT` |
| 12 | libparam descriptors | `KFSW_PARAM_LIST_PORT` |
| 13 | FWU lite uploads | `KFSW_FWU_LITE_CSP_PORT` |
| 14 | Housekeeping | `KFSW_HK_CSP_PORT` |
| 16 | Remote log (log history and journal) | `KFSW_LOG_REMOTE_PORT` |

Every node serves ports 0 and 1; libcsp's ports 2 to 6 are not served. Both
ends of a link must use the same port numbers. `CSP_PORT_MAX_BIND` is 16 and
raising it costs connection slots, so 7 and 8 are the room left for a new
service rather than 17 and up.

**Packet.** A CSP header (addresses, ports and flags such as CRC32 or RDP) and
a payload. Packets come from a fixed buffer pool and stay datagrams, also with
RDP.

**Connection.** libcsp state for a request and reply or an RDP exchange,
taken from a fixed pool.

**Interface.** Carries packets over a link such as CAN, KISS or loopback, with
an address and counters. Each KISS interface has its own UART, name, address
and framing state.

**Route.** Maps an address prefix to an interface and, optionally, a next hop.
The longest matching prefix wins. The router delivers packets for the local
node and forwards the rest.

A composition names its routes with `CONFIG_KFSW_CSP_ROUTE_TABLE`. The Linux
and NUCLEO images use `0/0 KISS`: every other node is reached over the serial
link. Without a table the node loads `0/0 LOOP` and reaches only itself, so a
link is never picked by whichever interface came up first. Routes are fixed
once the router starts.

More detail is in the libcsp
[basic architecture](https://github.com/libcsp/libcsp/blob/develop/doc/basic.md),
[protocol stack](https://github.com/libcsp/libcsp/blob/develop/doc/protocolstack.md)
and [topology](https://github.com/libcsp/libcsp/blob/develop/doc/topology.md)
documents.

## Radio encryption

Add `config/profiles/radio-crypto.conf` to both builds. The NUCLEO also needs
`nucleo-radio-crypto.overlay` for its hardware RNG. The radio module protects
the KISS interface named by `KFSW_RADIO_UHF_CRYPTO_INTERFACE`.

Generate a 256-bit key with `openssl rand -hex 32`, then on each node's
console:

```text
param set uhf_key_hex <64 hex digits>
param get uhf_crypto_error
comms uhf connect
comms uhf status
```

Use the same key on both ends and set `KFSW_CSP_UART_PEER_ADDRESS` on each
node. The key reads back empty and can't be written from another node. Radio
settings are saved outside the FTP directory.

`uhf_encrypt_enable` turns protection on or off, and `uhf_encrypt_tx` and
`uhf_encrypt_rx` select the directions; both default to 1. With RX enabled,
plaintext is rejected, and a missing key or session blocks traffic.

The link uses AES-256-GCM with a 16-byte tag, and the CSP header is
authenticated too. Sessions and sequence numbers reject replayed frames, also
after a reset. A replayed handshake can interrupt a session but can't restore
an old key; `comms uhf connect` starts a new handshake. With the default 256-byte
buffer and CRC32, an encrypted packet carries up to 220 bytes of data. Other
interfaces are not encrypted.

## Packet path

```text
get parameter "log_level"
        |
destination node 2, port 10
        |
libcsp: header and payload
        |
route table: 0/0 -> KISS
        |
KISS interface
        |
UART
```

On receive, the router hands the packet to the service bound to its port.
Services never read UART bytes.

## RDP

RDP is libcsp's reliable datagram transport: connection setup, a window,
acknowledgements, retransmission, reordering and flow control. Data still moves
as CSP datagrams; it is not TCP.

```text
FTP client                          FTP server
connect                       ->
                              <-    confirm
PUT metadata, seq=N           ->
                              <-    ACK N
file chunk, seq=N+1           ->    (lost)
file chunk, seq=N+1, resent   ->
                              <-    ACK N+1
                              <-    result and file CRC
close                         ->
```

FTP doesn't retry on top of RDP. It adds the offsets, sizes, file CRC and
temporary file that RDP can't check. See the
[libcsp RDP section](https://github.com/libcsp/libcsp/blob/develop/doc/protocolstack.md#rdp).

## Packet buffers

libcsp uses preallocated buffers:

- a packet from a receive call has to be freed or passed to a send or reply
  call;
- a packet passed to a send call is freed by libcsp, also when sending fails,
  so don't free or reuse it;
- an interface passes complete packets to the router queue;
- when the pool or a queue is full, the allocation fails or the packet is
  dropped and counted.

To retry, build a new packet.

## Startup

`kfsw-comms` sets up libcsp once:

1. Set the hostname, model, revision and address.
2. Call `csp_init()`.
3. Give the loopback interface the local address.
4. Create the KISS and CAN interfaces, then check and load the routes.
5. Bind the ping handler.
6. Start the router thread, which calls `csp_route_work()`.

Services bind their ports after `kfsw_csp_init()`. The application starts
the router before starting its remote services. The API gives the state,
interfaces, routes, free buffers and ping. `csp interfaces` and `comms uart info`
show the counters since boot.

## Counters

`csp interfaces` shows what each link carried. `csp counters` shows libcsp's
error counters:

| Counter | Raised when |
| --- | --- |
| `buffer_out` | No packet buffer was free |
| `conn_out` | No connection slot was free |
| `conn_ovf` | A full connection queue dropped a packet |
| `conn_noroute` | A packet had no route to its destination |
| `invalid_reply` | A reply matched no open connection |
| `last_error` | Code of the last libcsp error |
| `last_can_error` | Code of the last CAN framing error |

The counters are 8-bit, wrap at 255, and libcsp updates them without locking,
so read them as an indication. `csp counters clear` zeroes them before a run.
Parameter table 4 carries the same values, so a ground station reads them like
any other parameter.

## Remote interface counters

`csp ifstat 2 KISS` reads a named interface on node 2 through CSP management.
It reports packet and byte counts, transmit/receive errors, drops, authentication
and framing errors, and the driver's interrupt count. `csp interfaces` still
lists this node's interfaces; the remote name must be known beforehand.

The query has a one-second timeout. An unknown interface also times out because
CMP has no not-found reply. Counts can wrap and change while read, and the
query itself contributes traffic. Parameter table 4 continues to expose the
aggregate counters.

## Capturing traffic

[kfsw-csp-tools](https://github.com/dgonzalez97/kfsw-csp-tools) is an optional
host dependency pinned in `west.yml`. Its CAN/ZMQ transports, `cspdump` and
`csp-ping-server` use CSP 1. Their four-byte headers and original Wireshark
dissector do not match K-FSW's default CSP 2 configuration. The adapted
`csp-iperf` also supports CSP 2 over KISS.

The fork's `csp-kiss` entry point supports only CSP 2. It works directly on the native Linux
PTY or a serial KISS link. It implements ping, CMP interface statistics,
node discovery, remote log retrieval and passive capture.

From `k-fsw`, with Rust 1.88 or newer installed:

```bash
west update kfsw-csp-tools
./tools/kfsw-linux csp build
./tools/kfsw-linux run
```

Use the `uart_1 connected to pseudotty` path printed by that node in another
terminal. One program uses that PTY at a time:

```bash
./tools/kfsw-linux csp --device /dev/pts/7 ping --node 1 --count 5
./tools/kfsw-linux csp --device /dev/pts/7 ifstat --node 1 --interface KISS
./tools/kfsw-linux csp --device /dev/pts/7 dump --seconds 10 --pcap-file traffic.pcap
```

The source address defaults to 16; `--baud` defaults to 115200. Both KISS and
CSP CRC32C checks are verified. An unanswered ping or interface query fails.
A capture sends nothing and records packets transmitted by the node, such as
HK beacons or pings issued from its shell. It fails if no packets arrive.

PCAP uses LINKTYPE_USER0 (147), with the original six-byte CSP 2 header and
payload, including any CSP checksum. Only KISS framing and its outer checksum
are removed. The old CSP 1/ZMQ dissector does not decode this format. Output
files must be new. Host-tool builds are separate from firmware builds, and
normal west updates leave the `host-tools` group disabled.

## Echo throughput and RTT

```bash
./tools/kfsw-linux csp perf --device /dev/pts/7 --dest-addr 1 \
  --packet-size 64 --tx-rate 640 --duration 10 --reply-timeout 1 --json
```

`csp build` builds both host binaries. `perf` dispatches to `csp-iperf`; put
`perf` before its options. It uses the node's existing ping service and does
not change clocks or firmware configuration. The source address defaults to
30 (`--src-addr`), baud to 115200. The source must be unused and the link must
have only one host reader.

The offered rate is CSP bytes/second, including the CSP header/checksum but
excluding KISS framing. Replies must match both addresses, ports, a random
run ID, sequence and the full expected payload. RTT uses host monotonic time.
JSON reports unique replies, final loss, duplicate/reordered/late replies and
RTT. Silence and trailing loss count as loss; a reordered reply within its
deadline recovers its gap. The run ends after the sending duration and reply
drain, exiting 2 on any loss. Invalid rates fail before the device is opened.

TX byte rate uses the sending window; RX rates include the final drain and
its duration is reported as `elapsed_seconds`. Actual rate can be lower than
the offered rate. Native PTYs verify the protocol; only a real link shows its capacity.
Use 64-byte packets for the smoke test; larger sizes must fit the node's CSP
buffers. `--reply-size` is for a separately configured CSP 1 echo server;
K-FSW's standard ping echoes the original size. CAN/ZMQ remain CSP 1 only.
The bench fixture is in `tests/hil/diagnostics/README.md` in the application
checkout.

## Discovery

```bash
./tools/kfsw-linux csp --device /dev/pts/7 discover --nodes 1,2 --output nodes.jsonl
./tools/kfsw-linux csp --device /dev/pts/7 --source 100 discover --range 1:16 --budget-ms 5000
```

Another node's log and journal are read from a K-FSW shell with `log remote`
and `journal remote`; see the services guide.

`discover` queries explicit unicast addresses or an inclusive range, up to
64 addresses. It pings each node and then asks for CMP identity. It reports
`identified`, `reachable` without a valid identity, `no_reply`, `invalid_ping`,
or `not_queried` when the overall budget expires. An unanswered node is an
inventory result; an unfinished inventory exits nonzero. Its final summary
states whether every requested node was queried. `--timeout-ms` bounds each
exchange and `--budget-ms` bounds the whole inventory. Ctrl-C stops either
command; flushed records remain, without a successful completion marker.

The source address defaults to 16 and must not be included in the requested
nodes. The broadcast address, 16383, is excluded. `discover` observes nodes reachable through
configured CSP routes; it does not implement ARP, build a routing topology,
or detect duplicate addresses. Queries use the existing ping and CMP services
and make no configuration or clock changes.

### Log history wire format

All integers below are big endian. Requests and replies require CSP CRC32C;
the KISS link adds its own checksum. One request is served per connection.

| Message | Fields, in order |
| --- | --- |
| Request (12 bytes) | version u8 = 1, minimum severity u8, count u16 (1 to 32), nonce u64 |
| Reply header (10 bytes) | version u8 = 1, type u8, echoed nonce u64 |
| Start, type 0 (34 bytes total) | header, first sequence u64, exclusive end sequence u64, overwritten count u64 |
| Record, type 1 (30 to 220 bytes total) | header, sequence u64, uptime_ms u64, module u8, severity u8, truncated u8 (0/1), text length u8, up to 190 text bytes |
| End, type 2 (13 bytes total) | header, status u8, sent record count u16 |

End statuses: 0 finished, 1 a requested record was overwritten, 2 no packet
buffer for a record. Invalid requests receive no response. An exhausted packet
pool can also prevent start/end transmission, resulting in a client timeout.
The nonce distinguishes separate reads; it is not authentication. Module IDs
use `enum kfsw_log_module` from the services API. Reads do not generate a log
message themselves.

## Test topologies

The two-node software test connects two Linux processes through their
simulated UARTs:

```text
node 1                                                  node 2
router -- KISS -- native PTY -- socat -- native PTY -- KISS -- router
```

The second node uses `tests/config/linux-node2.conf`. On the NUCLEO bench,
node 2 is the board, connected through USART3 and an FTDI cable.

The multi-interface test runs a router with two links and a node on each:

```text
node 10                         router                         node 11
  KISS ---- PTY/socat ---- KISS_1 8/14   KISS_2 9/14 ---- PTY/socat ---- KISS
                                  |             |
                            10 -> KISS_1   11 -> KISS_2 via 11
```

The router's two interfaces have different addresses because libcsp doesn't
forward a packet between interfaces in the same subnet.

## Route table

`CONFIG_KFSW_CSP_ROUTE_TABLE` uses the libcsp parser's syntax:

```text
destination[/prefix-length] interface [via], next-entry
10 KISS_1,11 KISS_2 11
```

The prefix length counts address bits: 14 in CSP 2, 5 in CSP 1. No prefix
matches one node and `/0` every node, in both versions, so a table written
without prefixes or with `/0` builds for either; CSP 1 rejects a prefix above
`/5`. The multi-KISS test router uses `tests/config/multi-kiss-router-csp1.overlay`
under CSP 1 for the interfaces' own `/5` prefixes. Two entries with the same destination and prefix are both used,
not tried in order. `via` is the link-layer next hop; KISS ignores it, but
`csp routes` still shows it.

Ground node files can set the same table with `KFSW_CSP_ROUTES`. At startup
the table is checked with `csp_rtable_check()`, the interface names, the
parser's 99-character limit and the table size, and only then loaded. A bad
table fails initialization.

With several UARTs, each one is a child of a `kfsw,csp-kiss-uarts` devicetree
node. Interface names are one to nine characters from `[A-Za-z0-9_-]`, which is
the parser's limit.

## KISS

KISS frames CSP packets on a UART, escapes reserved bytes and passes decoded
packets to the router. KISS doesn't guarantee delivery or ordering; K-FSW uses CSP
CRC32 to detect corruption and RDP for file transfers.

- KFSW-Linux uses libcsp's Zephyr USART driver on a native_sim PTY.
- The NUCLEO-L496ZG passes USART3 bytes from the interrupt handler to libcsp's
  KISS decoder, which avoids overruns.

The reference profiles use 115200 8N1 and the Holybro profiles 57600. See the
[KISS interface](https://github.com/dgonzalez97/kfsw-libcsp/blob/kfsw/include/csp/interfaces/csp_if_kiss.h)
in the libcsp fork the build uses.

## CAN

CAN uses libcsp's CAN interface on the controller chosen with `kfsw,csp-can`.
Under CSP 2 frames use CFP2, the CAN Fragmentation Protocol for 14-bit
addresses; under CSP 1 they use CFP1 and its 5-bit addresses, and
`CONFIG_KFSW_CSP_CAN_PREFIX_LENGTH` defaults to 5. The fields are the `CFP2_*`
and `CFP_*` definitions in
[csp_if_can.h](https://github.com/dgonzalez97/kfsw-libcsp/blob/kfsw/include/csp/interfaces/csp_if_can.h).
The NUCLEO uses CAN1 on PD0/PD1 with an external transceiver, and a Linux node
uses a SocketCAN interface. Both ends of the bus need the same bitrate; the
profiles use 500 kbit/s.

### Virtual CAN on Linux

`vcan` carries CAN frames between processes on one host. To test two Linux
nodes without hardware:

```bash
sudo k-fsw/tests/vcan-up.sh
./k-fsw/tests/can-smoke.sh
```

The first command needs root and only has to run once after a boot. The test
runs a node at address 2 against the `kfsw-gnd-can` ground role at 16, pings
both ways, reads remote parameters and checks the counters afterwards.

The interface name comes from `--can-if` at run time, so the same images run
against a real adapter by naming `can0` instead. A virtual bus has no bit
timing, so it proves routing, framing and the services, not the wire.

## Shell UART and CSP UART

The NUCLEO has two serial connections:

```text
ST-LINK virtual COM port <--> shell and logs
USART3 PD8/PD9           <--> FTDI 3.3 V UART <--> CSP KISS packets
```

Don't mix them up: shell text on USART3 is invalid KISS, and KISS data on the
ST-LINK console is binary noise. On Linux the shell uses stdin/stdout and CSP a
separate PTY.

## UART bench

The UART hardware test connects KFSW-Linux node 1 to NUCLEO-L496ZG node 2
through an FTDI TTL-232R-3V3 on USART3, with the ST-LINK console connected
too. It flashes the board, checks both serial connections, pings both ways,
runs `comms uart test`, checks storage, transfers 4 KiB and 16 KiB files, reads a
remote parameter and checks the KISS counters.

## Security

Only the encrypted radio link is authenticated. libcsp HMAC is not enabled,
CRC32 only detects accidental corruption, and commands have no per-node
authorization.

The optional host binaries can be fetched and built with `tools/host-tools.sh`
(Rust required). `KFSW_HOST_TOOLS=1 tools/ci/robot.sh` opts into building them;
prebuilt binaries are detected automatically. Set `KFSW_OUTPUT_ROOT=/tmp/kfsw-ci`
to keep generated CI builds and reports outside the workspace.
