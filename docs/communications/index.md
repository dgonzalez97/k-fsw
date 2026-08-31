# CSP and Communications {#communications}

## CSP before K-FSW

[Cubesat Space Protocol (CSP)](https://github.com/libcsp/libcsp) is a compact
packet network designed for embedded and space systems. It gives applications
node addresses, service ports, packets, connections, routing, network
interfaces, and optional transport features without binding the application
protocol to one physical link.

K-FSW uses the upstream libcsp implementation. `kfsw-comms` owns how that
library is initialized and presented to the rest of K-FSW; it does not fork the
CSP protocol or hide its packet-ownership rules.

CSP is **optional**. `CONFIG_KFSW_CSP=n` builds a composition without libcsp
initialization, routes, a router thread, or CSP shell commands. Local
parameters and persistence are explicitly software-tested with CSP disabled.
Only CSP-backed adapters such as remote parameters and FTP require it.

## CSP vocabulary

### Node and address

A node is one CSP protocol endpoint: an on-board computer, subsystem,
development process, or ground-side tool. Its CSP address identifies it for
routing. Current K-FSW configurations use CSP version 2 addresses in the range
1 through 16383 and assign one address at build time with
`CONFIG_KFSW_CSP_ADDRESS`.

KFSW-Linux is node 1 by default and NUCLEO-L496ZG is node 2. Those values form
the current test topology; they are not a universal spacecraft address plan.
A real product must allocate addresses deliberately and keep them unique on
every connected CSP network.

### Port

A port identifies a service at a node. The destination pair “node 2, port 9”
means the FTP service on node 2, not a physical connector or operating-system
serial port. Current K-FSW service ports are:

| CSP port | Use | Configuration |
| --- | --- | --- |
| 9 | K-FSW file transfer | `KFSW_FTP_CSP_PORT` |
| 10 | libparam value transactions | `KFSW_PARAM_PORT` |
| 12 | libparam parameter-list descriptions | `KFSW_PARAM_LIST_PORT` |

libcsp also defines management/service ports, including the standard ping
service K-FSW registers. Port allocations are protocol interfaces and should
not be changed independently at opposite ends.

### Packet

A CSP packet contains a CSP header and an application payload. The header
carries source/destination addresses and ports plus flags for optional features
such as CRC32 or RDP. libcsp allocates packets from a fixed buffer pool and
passes their ownership between application, router, and interface code.

Packets are datagrams. Their boundaries matter. Even when RDP is enabled, CSP
does not become a byte stream.

### Connection

A CSP connection is a libcsp context associating local and remote endpoints
and receive state. Connection-oriented calls make request/reply and RDP flows
convenient, but application data is still sent and received as complete
packets. A connection consumes a slot from libcsp's configured connection
pool; it is not an unbounded host socket abstraction.

### Interface

An interface adapts CSP packets to a link-layer mechanism such as CAN, KISS,
I2C, ZMQ, or loopback. It has an address, a transmit function, receive path,
maximum-transfer constraints, and counters. Current K-FSW exposes libcsp's
loopback interface and one or more UART/KISS interfaces when selected. Every
KISS instance has a distinct UART, name, address/prefix, framing state,
transport context, and counters.

An interface is not a route. It says how packets can enter or leave; the
routing table says which interface to use for a destination.

### Route and router

A route maps an address prefix to an interface and, where required, a
link-layer next hop. More-specific prefixes win over broad ones. The router
takes complete packets from the incoming queue, delivers packets addressed to
the local node, or forwards non-local packets through a selected interface.

An existing one-interface composition with no explicit route table still
installs `0/0 -> KISS direct`. This is a direct default route: every non-local
destination is sent on that serial link without a separate gateway address.
Multi-interface compositions must provide explicit libcsp routes instead of
implicitly choosing the first link.

### Transport and framing

Transport features operate above routing and link interfaces. libcsp's RDP can
add acknowledgements, windowing, ordering, and retransmission to a connection.
CRC32 can detect packet corruption. A link framing scheme such as KISS performs
a different job: it finds packet boundaries and escapes control bytes on a
serial byte stream.

The upstream libcsp [basic architecture](https://github.com/libcsp/libcsp/blob/develop/doc/basic.md),
[protocol stack](https://github.com/libcsp/libcsp/blob/develop/doc/protocolstack.md),
and [topology guide](https://github.com/libcsp/libcsp/blob/develop/doc/topology.md)
provide the protocol-level detail beyond this chapter.

## A packet's path

An application chooses a destination node and port, constructs a service
payload, and gives the packet to libcsp. Routing and interface selection happen
below the service.

```text
application operation: get parameter "log_level"
                         |
                destination node 2
                parameter port 10
                         |
                      libcsp
                CSP header + payload
                         |
                  routing table
                  0/0 -> KISS
                         |
                  KISS interface
                         |
                UART physical link
```

The receive path reverses the link operations, then the router uses the
destination port to deliver the complete packet to the registered service.
K-FSW services never parse UART bytes directly.

## What `kfsw-comms` owns

`kfsw-comms` is the single owner of the shared libcsp lifecycle:

1. Set hostname, model, revision, and local address.
2. Call `csp_init()` once.
3. Assign the local address to libcsp loopback.
4. Create every configured KISS interface, then validate and load static routes.
5. Bind libcsp's standard ping handler.
6. Start one K-FSW router thread that repeatedly calls `csp_route_work()`.

The public API exposes lifecycle state, interface and route snapshots, buffer
availability, and a bounded ping operation. It keeps application code out of
libcsp global lists while retaining libcsp's semantics.

Services may register endpoints after `kfsw_csp_init()` and before
`kfsw_csp_start()`. They do not start another router or create an independent
CSP instance. The shell is a caller of the same API:

```text
kfsw:~$ csp info
kfsw:~$ csp interfaces
kfsw:~$ csp routes
kfsw:~$ csp ping 2
```

`csp interfaces` and `uart info` expose packet/error/drop counters. These are
diagnostic snapshots, not accumulated mission telemetry or persistent fault
records.

## Routing in the current topology

The native two-node software test creates two application processes and
connects their simulated UARTs:

```text
KFSW-Linux node 1                              node 2 test image
-----------------                              -----------------
service ports                                         service ports
      |                                                     |
   router                                                router
      |  default 0/0 -> KISS              default 0/0 -> KISS |
    KISS ---- native PTY ---- socat ---- native PTY ---- KISS
```

Both nodes can ping, access parameters, and transfer files in both directions.
The second node uses `tests/config/linux-node2.conf`; it is an integration
fixture rather than another supported product target.

The NUCLEO bench replaces node 2's PTY with a physical UART and an FTDI cable.
The application/service packet path stays the same, which is the point of the
interface boundary.

The deterministic multi-interface test creates a router with two different
native PTYs and two leaf processes:

```text
node 10                         router                         node 11
  KISS ---- PTY/socat ---- KISS_1 8/14   KISS_2 9/14 ---- PTY/socat ---- KISS
                                  |             |
                         10/14 -> KISS_1   11/14 -> KISS_2 via 11
```

It proves router-originated traffic selects each link, both interface counter
sets advance independently, the VIA field survives configuration/diagnostics,
and node 10 and node 11 can ping through the router in both directions. The
distinct `/14` interface addresses are significant: the pinned libcsp
split-horizon logic must see these as different links before it forwards a
transit packet.

There is currently no CAN/CFP interface, automatic route discovery, runtime
route mutation, redundant-link failover policy, ZMQ interface, or production
radio driver in the K-FSW composition. The k-ground Holybro HIL entry point
continues to reuse the direct serial KISS route. Its named one-link bench
passed 100/100 raw exchanges and bidirectional node 16 ↔ node 2 CSP ping with
clean KISS counters on 30 August 2026. No physical `KISS_2` bench is claimed.

## Route-table configuration

K-FSW reuses the pinned libcsp text parser rather than maintaining a second
route representation. `CONFIG_KFSW_CSP_ROUTE_TABLE` is a comma-separated list
in this exact form:

```text
destination[/prefix-length] interface [via], next-entry
10/14 KISS_1,11/14 KISS_2 11
```

CSP v2 node IDs are 14 bits. The mask is the count of most-significant address
bits in the prefix: `/14` matches one exact node, `/0` matches every node, and
an omitted mask defaults to `/14`. The longest matching prefix wins. Equal
destination/prefix entries are treated by this libcsp revision as multiple
eligible routes and may transmit on each, not as ordered fallbacks.

The optional `via` is libcsp's link-layer next-hop value. Its absence is stored
as `CSP_NO_VIA_ADDRESS`; its presence is passed to the selected interface and
reported by `csp routes`. KISS has no link-layer address, so the pinned KISS
transmit function intentionally ignores `via`. Other interface types may use
it; K-FSW does not reinterpret or discard it.

Embedded profiles set the table through Kconfig. Ground node files may set the
same value as `KFSW_CSP_ROUTES`, which `tools/k-ground` validates and writes to
the generated Kconfig fragment. Startup first validates the complete table
with `csp_rtable_check()`, interface-name resolution, the parser's 99-character
limit, and route-table capacity, then loads it. Malformed or unknown-interface
tables fail initialization. Runtime shell loading is intentionally absent:
the pinned loader mutates incrementally, so a generally safe live replacement
would require synchronization and rollback semantics outside this issue.

Multi-interface devicetree uses enabled children of a
`kfsw,csp-kiss-uarts` node. Interface names are one through nine
`[A-Za-z0-9_-]` characters because the pinned route parser reads at most nine;
`KISS_1` and `KISS_2` are valid, but longer examples must be shortened.

## KISS: packets on a serial stream

A UART transports a sequence of bytes. It does not preserve writes as packet
boundaries: a receiver may read part of one write, several writes together, or
data split at any position. A receiver therefore needs framing to distinguish
one complete CSP packet from the next.

KISS supplies delimiter and escape rules. Frame boundary bytes identify the
start/end of a frame, and occurrences of reserved bytes inside the packet are
escaped. On receive, the KISS state machine removes framing and passes a
complete CSP packet to the router.

```text
               transmit                            receive

              CSP packet                         CSP packet
                  |                                  ^
           add KISS framing                    remove framing
                  |                                  ^
              UART bytes  -------------------->  UART bytes
                  |                                  ^
           physical transport  ----------------------+
```

KISS provides framing; it does not itself guarantee delivery, correct ordering
across lost frames, authentication, or encryption. K-FSW uses CSP CRC32 where
selected for corruption detection and RDP for reliable FTP delivery.

libcsp's maintained KISS interface performs packet framing. K-FSW supplies
each Zephyr UART device and one of two receive paths:

- KFSW-Linux uses libcsp's Zephyr USART backend over a native-simulator PTY.
- NUCLEO-L496ZG configures USART3 and feeds interrupt-driven receive bytes to
  libcsp's KISS decoder to avoid physical polling overruns.

Both use 115200 baud, eight data bits, no parity, and one stop bit in the
reference profiles. The Holybro HIL overlays select 57600 baud at both ends.
The legacy chosen devicetree node is the source of those values for a single
link. Multi-link profiles place a UART phandle and independent interface
configuration in each `kfsw,csp-kiss-uarts` child.
The [libcsp KISS interface API](https://github.com/libcsp/libcsp/blob/develop/include/csp/interfaces/csp_if_kiss.h)
is the upstream reference for framing state and interface hooks.

## Shell UART versus CSP UART

The NUCLEO uses two serial paths:

```text
ST-LINK virtual COM port <----> Zephyr shell and K-FSW logs

USART3 PD8/PD9 <----> FTDI 3.3 V UART <----> CSP KISS packets
```

They are not multiplexed. Sending shell text to USART3 produces invalid KISS
input; sending KISS data to the ST-LINK console displays binary noise and does
not reach CSP. The Linux native simulator likewise uses stdin/stdout for the
interactive shell and a separately reported PTY for CSP.

## Physically verified UART bench

The merged physical HIL path connects KFSW-Linux node 1 to NUCLEO-L496ZG node
2 using an FTDI TTL-232R-3V3 adapter:

```text
KFSW-Linux node 1
      |
native_sim KISS PTY
      |
    socat
      |
FTDI 3.3 V UART
      |
USART3 PD8/PD9
      |
NUCLEO-L496ZG node 2

ST-LINK remains a separate shell/log connection to the NUCLEO.
```

The acceptance runner flashes the NUCLEO, verifies both service and KISS
serial paths, performs bidirectional CSP ping, runs the permanent UART test,
checks storage, transfers 4 KiB and 16 KiB files with FTP, accesses a remote
parameter afterward, and requires nonzero clean KISS counters.

This is physical UART/KISS qualification for the current NUCLEO/FTDI bench.
Separately, the Holybro/SiK fixture described in @ref ground now has functional
raw UART/RF and CSP/KISS bench evidence. That result verifies the named
hardware path and configuration; it is not RF performance or flight
qualification.

## RDP: reliable CSP datagrams

RDP is libcsp's reliable datagram transport. It establishes connection state,
tracks packets in a bounded window, acknowledges received data, retransmits
unacknowledged data after timeouts, buffers/reorders data as required, and
applies flow control.

RDP is not TCP. It preserves CSP datagrams, uses libcsp's configuration and
packet pools, and implements a different protocol. Code and operations should
call it CSP RDP, not a TCP stream.

A simplified successful exchange is:

```text
FTP client                                      FTP server
    |                                               |
    |------ RDP connection establishment ---------->|
    |<---------------- confirmation -----------------|
    |                                               |
    |------ CSP packet: PUT metadata, seq=N -------->|
    |<---------------- ACK through N ----------------|
    |------ CSP packet: file chunk, seq=N+1 -------->|
    |          [packet or ACK lost]                  |
    |------ retransmit after RDP timeout ----------->|
    |<---------------- ACK through N+1 --------------|
    |------ remaining ordered datagrams ------------>|
    |<------ application result + final CRC ---------|
    |------ close ----------------------------------->|
```

Acknowledgements and retransmission are libcsp responsibilities. K-FSW FTP
does not add a second retry protocol. It adds application-level metadata,
offsets, bounded chunks, expected total size, file CRC, temporary files, and
final commit rules because reliable packet delivery alone cannot prove that a
complete file is valid and safely installed.

The upstream [libcsp protocol-stack RDP section](https://github.com/libcsp/libcsp/blob/develop/doc/protocolstack.md#rdp)
describes its handshake, flow control, reordering, retransmission, and
windowing.

## Packet ownership and bounded failure

libcsp is designed around preallocated buffers and mostly zero-copy packet
movement. K-FSW follows these ownership rules:

- a packet returned by a receive API belongs to the receiver until freed or
  handed to a send/reply API;
- handing a packet to a send operation transfers ownership, even if a lower
  interface later reports a transmit failure;
- a complete interface receive transfers the packet to the router queue; and
- pool or queue exhaustion is a bounded allocation failure or counted drop,
  not a reason to create an unbounded retry list.

An application that needs to retry after a send must construct new state at
the appropriate layer. It must not resend or free a transferred packet.

## Security boundary

The current K-FSW composition does not enable libcsp HMAC or provide link
encryption, authentication policy, key management, or command authorization.
CRC32 detects accidental corruption; it is not a cryptographic integrity
mechanism. A test cable or closed bench network does not establish the security
properties required for an operational radio or spacecraft network.

Adding a physical link therefore requires more than a driver. It also requires
an address/routing plan, bandwidth and MTU analysis, failure behavior,
operational access policy, and security design appropriate to that link.
