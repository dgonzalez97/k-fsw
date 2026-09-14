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
