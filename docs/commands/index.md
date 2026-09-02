# Shell and Command Reference {#commands}

## The Zephyr shell in K-FSW

Development profiles enable Zephyr's serial shell with history, tab
completion, metakeys, and command help. K-FSW registers project commands at
the shell root and lets Zephyr supply line editing, dispatch, usage validation,
and built-in commands.

The prompt is configured as:

```text
kfsw:~$
```

This text identifies the console. It is not a command namespace. Type:

```text
kfsw:~$ status
```

not:

```text
kfsw:~$ kfsw status
```

Documentation examples include the prompt to distinguish commands from
output. Copy only the text after `$` when using a terminal adapter that does
not strip prompts.

## Editing, history, and discovery

Press `Tab` to complete a unique command or show matching commands. Use
`help` to list the root command set and `help <command>` or `<command> help`
where Zephyr's command tree offers contextual usage. Up/Down history and normal
line editing work when the attached terminal sends supported metakey escape
sequences.

```text
kfsw:~$ csp <Tab>
info  interfaces  ping  routes

kfsw:~$ help
kfsw:~$ param help
```

`Tab` completes command and subcommand names. It cannot complete free-form
arguments such as a node number or a path, so each command states its arguments
in its own help line. Three ways to reach that line:

```text
kfsw:~$ ftp                       group help plus every subcommand
kfsw:~$ ftp generate -h           one subcommand's usage
kfsw:~$ ftp generate              wrong argument count prints the same usage
```

```text
kfsw:~$ ftp generate
generate: wrong parameter count
generate - Create deterministic local data: generate <path> <bytes 0..32768>.
```

Printing usage on a wrong argument count is enabled by `KFSW_DEBUG_SHELL`; it
applies to every command, not only `ftp`.

Command availability is a build property. A shell-only target will not show
`uhf`, `csp`, `uart`, `param`, `storage`, or `ftp` because their Kconfig owners
are disabled. The shell does not provide placeholder commands for absent
services.

The shell thread and prompt may be active before the K-FSW startup sequence has
printed `@READY`. Wait for that marker, then query service-specific state.

## Command domains

```text
root
├── status, time, version
├── log
│   └── test
├── uhf                       when KFSW_RADIO_UHF_SHELL=y
│   └── status
├── csp                       when KFSW_CSP=y
│   ├── info, interfaces, routes, ping
├── uart                      when KFSW_CSP_KISS_UART=y
│   ├── info, test
├── param                     when KFSW_PARAM=y
│   ├── list, get, set
│   └── save, load, defaults, clear
│       when persistence is enabled
├── boton_test                when KFSW_BOTON_TEST_SHELL=y
│   └── status
├── test                      when KFSW_BOTON_TEST_SHELL=y
│   └── led <colour> <on|off>
├── storage                   when KFSW_STORAGE=y
│   ├── info, test
└── ftp                       when KFSW_FTP=y
    ├── list/ls, stat, mkdir, put, get
    └── generate, verify      diagnostic helpers
```

## Runtime identity and time

| Command | Arguments | Meaning |
| --- | --- | --- |
| `status` | none | Print configured role/name, CSP node when enabled, compiled Zephyr board target, and monotonic uptime |
| `version` | none | Print K-FSW development revision text, Zephyr kernel version, and board target |
| `time` | none | Print monotonic milliseconds and microseconds |

```text
kfsw:~$ status
K-FSW status
Role: flight
Name: kfsw
CSP node: 1
board: native_sim/native/64
uptime_ms: ...
```

Role and name are composition metadata; they do not select hidden behavior.
`status` does not aggregate service health or restate `@READY`. `time` is
elapsed local time, not UTC/TAI/GNSS or synchronized spacecraft time.

## Logging

| Command | Arguments | Meaning |
| --- | --- | --- |
| `log test` | none | Emit one message at every log level compiled into the image |

Runtime filtering may suppress lower-severity test lines. Change the
`log_level` parameter to exercise that callback; save it explicitly only if
the new threshold should survive reboot.

## UHF radio diagnostics

`uhf status` exists only when the reusable UHF module and its shell adapter are
enabled. It reports the selected implementation, expected hardware and serial
contract, hardware-status availability, and RF-link knowledge.

```text
kfsw-gnd-uhf# uhf status
UHF radio
enabled: yes
implementation: holybro-sik
expected hardware: RFD SiK 2.0 on HM-TRP
configuration source: build-time expectation, not hardware readback
expected serial: 57600 8N1
expected flow control: none
hardware status: unavailable
RF link: unknown
```

The command does not enter SiK command mode or read the modem. Expected values
must be compared with `uart info`; actual interface traffic and errors remain
under `csp interfaces` and `uart info`.

## `boton_test` diagnostic

The module-owned diagnostic exists only when `CONFIG_KFSW_BOTON_TEST_SHELL=y`:

| Command | Arguments | Meaning |
| --- | --- | --- |
| `boton_test status` | none | Print one coherent snapshot of button and LED state |
| `test led` | `<green|blue|red> <on|off>` | Set one developer LED through the module owner |

```text
kfsw:~$ boton_test status
press_count: 0
last_press_s: 0
led_green: off
led_blue: off
led_red: off
debounce_ms: 30
```

`debounce_ms` is printed by GPIO-enabled compositions; a software-only build
can omit that line.

The status command calls `kfsw_boton_test_get_status()`; it does not read GPIO
or look up parameters. `test led` and writable LED PARAMs call the same owner
setter, so they cannot create parallel GPIO state. Unknown colours and states
other than `on`/`off` are rejected without changing state. The module
intentionally provides no production
`fake-press` or reset command. Holding the button does not generate repeated
counts, and state resets only when the image reboots.

## CSP diagnostics

These commands exist only with `CONFIG_KFSW_CSP=y`.

| Command | Arguments | Meaning |
| --- | --- | --- |
| `csp info` | none | Show local address/identity, initialization state, router state, and free packet buffers |
| `csp interfaces` | none | List registered interfaces with addresses and packet/error/drop counters |
| `csp routes` | none | List address prefixes, selected interface, and optional next hop |
| `csp ping` | `<node>` | Send libcsp's standard ping with CRC32, bounded payload, and a one-second timeout |

```text
kfsw:~$ csp info
kfsw:~$ csp routes
0/0 -> KISS direct
kfsw:~$ csp ping 2
CSP ping 2: success, rtt_ms=...
```

A multi-interface composition exposes the names and next hop without collapsing
them to a generic transport:

```text
kfsw:~$ csp routes
10/14 -> KISS_1 direct
11/14 -> KISS_2 via 11
kfsw:~$ csp interfaces
KISS_1 addr=8/14 ...
KISS_2 addr=9/14 ...
```

Routes are static startup configuration. The debug shell intentionally has no
route-load/save command; `csp routes` is inspection-only.

A ping timeout can mean no peer, no physical bridge, wrong address, wrong
route, a stopped router, framing errors, or packet exhaustion. Inspect
`csp interfaces`, `csp routes`, and `uart info` before treating every timeout
as an application-service failure.

## Dedicated UART/KISS diagnostics

These commands exist only with `CONFIG_KFSW_CSP_KISS_UART=y`.

| Command | Arguments | Meaning |
| --- | --- | --- |
| `uart info` | none | Show every configured UART, baud, readiness, KISS name/address, and independent counters |
| `uart test` | `[node]` | Resolve the configured peer or explicit node through CSP, require a managed UART/KISS route, and ping with a 128-byte payload |

`uart test` is stronger than a generic ping because it first verifies route
selection and reports the interface name that libcsp selected. Use an explicit
node to distinguish links in a multi-interface image, for example `uart test
10` and `uart test 11`. It does not test every service or electrical condition.

On NUCLEO, these commands are entered through the ST-LINK shell while the
tested packets leave on USART3. Do not connect the shell terminal to the CSP
UART.

## Local parameters

Local commands exist with `CONFIG_KFSW_PARAM=y`.

| Command | Arguments | Meaning |
| --- | --- | --- |
| `param list` | none | List local ID, type, access, name, value metadata, and description |
| `param get` | `<name>` | Read one local scalar |
| `param set` | `<name> <value>` | Validate and change one local scalar in RAM |

```text
kfsw:~$ param list
kfsw:~$ param get log_level
log_level = 1
kfsw:~$ param set log_level 2
log_level = 2
```

`node_id` is read-only. `log_level` accepts 0 through 4. Integer parsing rejects
overflow and unsigned-negative input; the command reads the parameter
description first so it can parse the exact type.

The opt-in NUCLEO hardware-test profile additionally exposes five module-owned
live values. The button values remain read-only; LED values are writable
non-persistent booleans and accept only `0` or `1`:

```text
kfsw:~$ param get boton_test_press_count
boton_test_press_count = 0
kfsw:~$ param get boton_test_last_press_s
boton_test_last_press_s = 0
kfsw:~$ param get hw_test_led_green
hw_test_led_green = 0
kfsw:~$ param set hw_test_led_green 1
hw_test_led_green = 1
```

All five reset to zero/off on boot. A rejected `param set`, including an LED
value other than `0` or `1`, changes neither typed owner state nor GPIO output.

## Remote parameters

When `CONFIG_KFSW_PARAM_CSP=y`, `list`, `get`, and `set` accept an explicit
node before the normal arguments:

| Command | Meaning |
| --- | --- |
| `param list <node>` | Refresh and list the remote descriptor cache for that node |
| `param get <node> <name>` | Read a remote scalar over CSP |
| `param set <node> <name> <value>` | Validate text against the remote descriptor and request a remote RAM write |

```text
kfsw:~$ param get 2 log_level
2:log_level = 1
kfsw:~$ param set 2 log_level 2
2:log_level = 2
```

The same adapter can observe the button module on node 2:

```text
kfsw:~$ param get 2 boton_test_press_count
2:boton_test_press_count = 0
kfsw:~$ param get 2 boton_test_last_press_s
2:boton_test_last_press_s = 0
```

Remote writes to either value fail as read-only. This visibility does not make
`boton_test` a CSP-aware module; PARAM/CSP and routing remain separate
composition capabilities.

The node argument is decimal. Remote changes are not automatically persisted
on the destination. The current API has no remote “save” command.

When the CSP adapter is disabled, optional node arguments are not accepted;
the same command names remain local-only.

## Parameter persistence

These commands exist with `CONFIG_KFSW_PARAM_PERSISTENCE=y` and always act on
the local node.

| Command | Effect |
| --- | --- |
| `param save` | Build, CRC, sync, and atomically replace the saved snapshot from persistent RAM entries |
| `param load` | Validate and apply compatible entries from the saved snapshot |
| `param defaults` | Restore persistent entries to compiled defaults in RAM; saved file unchanged |
| `param clear` | Remove active/temporary snapshots; RAM unchanged |

A safe trial sequence is:

```text
kfsw:~$ param get log_level
kfsw:~$ param set log_level 2
kfsw:~$ param get log_level
kfsw:~$ param defaults
kfsw:~$ param get log_level
```

Only run `param save` after deciding the runtime state should become the next
boot's restored state. @ref services explains the complete state model.

## Storage

Storage commands exist with `CONFIG_KFSW_STORAGE=y`.

| Command | Arguments | Meaning |
| --- | --- | --- |
| `storage info` | none | Show filesystem, flash backend, `/kfsw` mount point, readiness, total bytes, and free bytes |
| `storage test` | none | Run bounded create/write/read/overwrite/delete diagnostics |
| `storage test write` | `<value>` | Write the integration persistence fixture |
| `storage test read` | `<value>` | Read and compare the integration persistence fixture |

```text
kfsw:~$ storage info
kfsw:~$ storage test
Storage test: PASS
```

`storage test` modifies only its diagnostic path, but it is still a write test.
Use `storage info` when a read-only readiness check is sufficient.

## File transfer

FTP commands exist with `CONFIG_KFSW_FTP=y`. K-FSW FTP is not Internet FTP;
all paths are virtual paths below `/kfsw/ftp` on the selected node.

The preferred operator syntax places the node first for operations that act on
a remote server:

| Command | Arguments | Meaning |
| --- | --- | --- |
| `ftp <node> ls` | `[remote-directory]` | List one remote directory; `list` is an alias |
| `ftp <node> stat` | `<remote-path>` | Report remote type, byte size, and file CRC |
| `ftp <node> mkdir` | `<remote-directory>` | Create one directory whose parent exists |
| `ftp <node> put` | `<local-path> <remote-path>` | Upload and atomically finalize a file |
| `ftp <node> get` | `<remote-path> <local-path>` | Download and atomically finalize a file |

Verb-first forms such as `ftp put <node> ...` remain available because they
map naturally to Zephyr static subcommands and completion.

### The local node

`ls`, `list`, `stat`, and `mkdir` accept this node's own CSP address. Those
requests are served directly from local storage: no connection is opened, no
route is used, and the result does not depend on any peer being reachable.

```text
kfsw:~$ csp info
CSP node: 1
kfsw:~$ ftp 1 ls /
kfsw:~$ ftp 1 mkdir /exchange
```

The local node still needs storage mounted and the FTP service started;
otherwise the command reports `service or storage not ready`.

`put` and `get` move a file between two nodes. Addressing them at the local
node reports `transfers need two nodes` rather than copying in place. Use
`ftp generate` and `ftp verify` for local file work.

Local diagnostic helpers are:

| Command | Arguments | Meaning |
| --- | --- | --- |
| `ftp generate` | `<local-path> <bytes>` | Generate deterministic data, up to 32768 bytes |
| `ftp verify` | `<first-local-path> <second-local-path>` | Compare two local sandbox files byte for byte |

A complete round trip is:

```text
kfsw:~$ ftp generate /build/sample.bin 1024
kfsw:~$ ftp 2 mkdir /exchange
kfsw:~$ ftp put 2 /build/sample.bin /exchange/sample.bin
kfsw:~$ ftp stat 2 /exchange/sample.bin
kfsw:~$ ftp 2 ls /exchange
kfsw:~$ ftp get 2 /exchange/sample.bin /build/returned.bin
kfsw:~$ ftp verify /build/sample.bin /build/returned.bin
```

Paths must begin with `/` and may not contain traversal or empty components.
An FTP result line reports node, paths, byte count, or the mapped failure. A
successful transport does not imply that a later `verify` can be skipped when
an operator needs an explicit local comparison.

## Automation guidance

Prefer stable markers and result lines over terminal timing. Wait for
`@READY`, send one command, assert its command-specific output, and wait for
the prompt before sending the next command. The project integration and Robot
runners follow this pattern.

Do not parse cosmetic spacing as a protocol. Shell output is an operator/test
interface; CSP service formats and public C return contracts are the machine
interfaces. When a script needs to span nodes, use the existing service API or
test fixture instead of sending a shell string to the remote node.
