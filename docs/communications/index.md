# Communications {#communications}

## CSP

K-FSW integrates libcsp behind the public headers in `kfsw-comms`. The
communications lifecycle initializes the CSP identity, interfaces, and static
routes once, then starts one application-owned router thread.

The shell provides status and diagnostic commands:

```text
kfsw:~$ csp info
kfsw:~$ csp interfaces
kfsw:~$ csp routes
kfsw:~$ csp ping 2
```

Parameter and FTP services call their K-FSW APIs and communicate through CSP.
They do not send shell command strings to a remote node.

## Routing

The supported native and physical targets install the KISS interface as the
direct default route. `csp routes` reports each address/prefix, interface, and
optional next hop; `csp interfaces` reports packet and error counters.

Node 2 is the current integration-test peer, not a hard-coded product topology.
Operator commands accept a CSP node in the range supported by their service.

## UART / KISS

KISS frames CSP packets over a dedicated UART. The target overlay selects that
device through the `kfsw,csp-uart` chosen property and provides its speed,
pins, and framing.

- KFSW-Linux exposes a native-simulator PTY for the KISS UART.
- NUCLEO-L496ZG uses USART3 on PD8/PD9 at 115200 baud, 8N1.
- The debug shell remains on native stdin/stdout or the ST-LINK virtual COM
  port; it is not multiplexed onto the CSP transport.

The software integration runner starts two simulated nodes, discovers both
PTYs, connects them with `socat`, and verifies bidirectional CSP operations.
The physical runner bridges the Linux PTY to an FTDI cable and is explicitly
tagged `physical`.

## RDP

The FTP service opens CSP connections with RDP and CSP CRC32. RDP supplies
ordered delivery, retransmission, and flow control. The K-FSW application
protocol adds file metadata, bounded chunks, total-size validation, and a file
CRC32, but it does not add a second transport retry mechanism.

## Packet ownership

K-FSW follows libcsp zero-copy ownership rules:

- A received packet belongs to the receiver until it is freed or passed to a
  CSP send/reply operation.
- Passing a packet to a send operation transfers ownership even when the
  interface later reports a transmit failure.
- A complete interface receive transfers the packet to the CSP router queue.
- Pool or queue exhaustion causes a bounded allocation failure or counted
  drop; project code does not create an unbounded retry queue.

Browse @ref kfsw_comms for the public communications API map.
