# Ground Composition {#ground}

K-FSW can be composed as a lightweight Linux ground node without creating a
second framework. `k-ground` uses the same Zephyr `native_sim` application,
shell commands, libcsp router, KISS interface, services, and build machinery as
KFSW-Linux. Its configuration changes the operator identity and CSP address;
it does not fork the service implementations.

```text
                        reusable K-FSW components
                                  |
                     +------------+------------+
                     |                         |
              flight profiles          k-ground profile
                                               |
                                         CSP / KISS
                                               |
                                      other K-FSW nodes
```

## Identity and node convention

Ground-side K-FSW addresses start at 16 by project convention. This is not a
CSP protocol restriction. The initial names are also conventions rather than
master/slave behavior:

| Instance | CSP address | Meaning |
| --- | --- | --- |
| `main` | 16 | Primary ground instance by convention |
| `peer` | 17 | Second local demonstration node |
| another name | 18 or higher | Additional ground instance |

The launcher validates ground addresses in `16..16383`; the peer may be any
different CSP v2 address in `1..16383`, which allows a ground node to target a
flight-side address. Node, name, hostname, peer route, and build directory are
generated per instance rather than hard-coded into C.

`status` uses the normal root command and makes the composition visible:

```text
k-ground# status
K-FSW status
Role: ground
Name: main
CSP node: 16
board: native_sim/native/64
```

Normal Linux images retain the `kfsw:~$` prompt and report `Role: flight`.

## Launching k-ground

Build or run one instance from the west workspace root:

```bash
./k-fsw/tools/k-ground build --node 16 --name main --peer 17
./k-fsw/tools/k-ground run --node 16 --name main --peer 17
```

The `run` command exposes the native KISS UART as a PTY, just like the regular
Linux target. A standalone instance still needs that PTY connected to a peer
transport before CSP can leave the process.

For the complete local demonstration, one command starts nodes 16 and 17,
bridges their KISS PTYs with `socat`, and presents node 16's shell:

```bash
./k-fsw/tools/k-ground demo
```

Then use the existing command syntax:

```text
k-ground# status
k-ground# csp ping 17
CSP ping 17: success, rtt_ms=...
```

The automated form verifies identity, prompts, and ping in both directions:

```bash
./k-fsw/tools/k-ground test
```

`main` currently changes only the generated name and hostname. There is no
central orchestration, master election, database, web service, or new command
framework.

## UHF and Holybro boundary

The HIL tree models the radio category separately from its implementation:

```text
tests/hil/radio-uhf/
└── holybro/
    ├── raw-smoke.sh
    └── csp-kiss-smoke.sh
```

`kfsw-comms` continues to own reusable CSP/KISS behavior. The current
`kfsw-modules` checkout has no committed baseline and is not in `west.yml`, so
this prototype does not pretend to add a reusable radio driver there. If that
repository is activated later, reusable radio-management code should follow
the category/device hierarchy `radio-uhf/holybro`.

The two pending physical tests answer different questions:

- `raw-smoke.sh` sends `KGROUND-RAW-PING 0001` and requires
  `KGROUND-RAW-PONG 0001`. It proves only that bytes traverse the UART/RF link.
- `csp-kiss-smoke.sh` builds k-ground node 16 and NUCLEO node 2 with 57600-baud
  UART profiles, bridges the ground KISS PTY to the USB radio, and requires a
  CSP ping. It proves KISS framing and CSP routing in addition to the link.

The recorded USB-side RFD SiK 2.0 settings and exact future commands are in
`tests/hil/radio-uhf/holybro/README.md`. Neither test is physically verified
until both radios, the NUCLEO-side wiring, and the appropriate raw or CSP peer
are present.
