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
ground tool. K-FSW uses CSP version 2, with addresses from 1 to 16383, set by
`CONFIG_KFSW_CSP_ADDRESS`. KFSW-Linux is node 1 by default and the
NUCLEO-L496ZG node 2. Every node on a network needs its own address.

**Port.** A service on a node: "node 2, port 9" is the file transfer service on
node 2.

| Port | Service | Option |
| --- | --- | --- |
| 9 | File transfer | `KFSW_FTP_CSP_PORT` |
| 10 | libparam values | `KFSW_PARAM_PORT` |
| 11 | Commands | `KFSW_COMMAND_CSP_PORT` |
| 12 | libparam descriptors | `KFSW_PARAM_LIST_PORT` |
| 13 | FWU lite uploads | `KFSW_FWU_LITE_CSP_PORT` |
| 14 | Housekeeping | `KFSW_HK_CSP_PORT` |
| 16 | Log history | `KFSW_LOG_HISTORY_PORT` |

Port 0 is libcsp's management service and port 1 its ping. Both ends of a link
must use the same port numbers.

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

A single-interface composition without a route table gets
`0/0 -> KISS direct`: every other node is reached over that serial link.
Compositions with more than one interface need a route table. Routes are fixed
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
uhf connect
uhf status
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
an old key; `uhf connect` starts a new handshake. With the default 256-byte
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

## Startup

`kfsw-comms` sets up libcsp once:

1. Set the hostname, model, revision and address.
2. Call `csp_init()`.
3. Give the loopback interface the local address.
4. Create the KISS and CAN interfaces, then check and load the routes.
5. Bind the ping handler.
6. Start the router thread, which calls `csp_route_work()`.

Services bind their ports after `kfsw_csp_init()` and before
`kfsw_csp_start()`. The API gives the state, interfaces, routes, free buffers
and ping. `csp interfaces` and `uart info` show the counters since boot.

## Counters

`csp interfaces` shows what each link carried. `csp counters` shows libcsp's
own error counters, which are the first thing to read when a link misbehaves:

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
host dependency pinned in `west.yml`. Its original `cspdump`, `csp-iperf` and
`csp-ping-server` use CSP 1 over CAN/ZMQ. Their four-byte headers and original
Wireshark dissector do not match K-FSW's CSP 2 configuration.

The fork's `csp-kiss` entry point supports CSP 2 directly on the native Linux
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

## Remote text logs and discovery

```bash
./tools/kfsw-linux csp --device /dev/pts/7 logs --node 1 --output logs.jsonl
./tools/kfsw-linux csp --device /dev/pts/7 logs --node 1 --count 16 --min-level 2
./tools/kfsw-linux csp --device /dev/pts/7 discover --nodes 1,2 --output nodes.jsonl
./tools/kfsw-linux csp --device /dev/pts/7 --source 100 discover --range 1:16 --budget-ms 5000
```

`logs` considers the latest 1 to 32 retained records, then filters by severity
(0 debug, 1 info, 2 warning, 3 error). JSONL contains a start record, log
records and an end record with `complete: true` only after all expected
replies arrive. `text_hex` preserves the original bytes; `text` replaces
invalid UTF-8 with replacement characters. Output files must be new, and
each record is flushed. A failed transfer keeps partial output and exits
nonzero. An absent end record also means incomplete output. The global
`--timeout-ms` is the budget for the entire log transfer; raise it for slow
links. Use `--port` inside `logs` when the node's log port differs from 16.

`discover` queries explicit unicast addresses or an inclusive range, up to
64 addresses. It pings each node and then asks for CMP identity. It reports
`identified`, `reachable` without a valid identity, `no_reply`, `invalid_ping`,
or `not_queried` when the overall budget expires. An unanswered node is an
inventory result; an unfinished inventory exits nonzero. Its final summary
states whether every requested node was queried. `--timeout-ms` bounds each
exchange and `--budget-ms` bounds the whole inventory. Ctrl-C stops either
command; flushed records remain, without a successful completion marker.

The source address defaults to 16 and must not be included in the requested
nodes. Address 16383 is excluded. `discover` observes nodes reachable through
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
| Record, type 1 (30 to 221 bytes total) | header, sequence u64, uptime_ms u64, module u8, severity u8, truncated u8 (0/1), text length u8, text bytes |
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
                         10/14 -> KISS_1   11/14 -> KISS_2 via 11
```

The router's two interfaces have different addresses because libcsp doesn't
forward a packet between interfaces in the same subnet.

## Route table

`CONFIG_KFSW_CSP_ROUTE_TABLE` uses the libcsp parser's syntax:

```text
destination[/prefix-length] interface [via], next-entry
10/14 KISS_1,11/14 KISS_2 11
```

Node IDs are 14 bits: `/14` matches one node, `/0` every node, and no prefix
means `/14`. Two entries with the same destination and prefix are both used,
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

A UART carries bytes, not packets. KISS framing marks where each packet starts
and ends and escapes reserved bytes, and the decoder passes complete packets
to the router. KISS doesn't guarantee delivery or ordering; K-FSW uses CSP
CRC32 to detect corruption and RDP for file transfers.

- KFSW-Linux uses libcsp's Zephyr USART driver on a native_sim PTY.
- The NUCLEO-L496ZG passes USART3 bytes from the interrupt handler to libcsp's
  KISS decoder, which avoids overruns.

The reference profiles use 115200 8N1 and the Holybro profiles 57600. See the
[libcsp KISS interface](https://github.com/libcsp/libcsp/blob/develop/include/csp/interfaces/csp_if_kiss.h).

## CAN

CAN uses libcsp's CFP interface on the controller chosen with `kfsw,csp-can`.
The NUCLEO uses CAN1 on PD0/PD1 with an external transceiver, and a Linux node
uses a SocketCAN interface. Both ends of the bus need the same bitrate; the
profiles use 500 kbit/s.

### A bus without hardware

`vcan` is a kernel interface that carries CAN frames between processes on one
host, so two Linux nodes reach each other over CSP with no controller, no
transceiver and no wiring:

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
runs `uart test`, checks storage, transfers 4 KiB and 16 KiB files, reads a
remote parameter and checks the KISS counters.

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

## Security

Only the encrypted radio link is authenticated. libcsp HMAC is not enabled,
CRC32 only detects accidental corruption, and commands have no per-node
authorization.
