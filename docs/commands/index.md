| `ftp ls` | `[node] [directory]` | List a directory |
| `ftp stat` | `[node] <path>` | Type, size and CRC |
| `ftp mkdir` | `[node] <directory>` | Create a directory |
| `ftp put` | `<node> <local> <remote>` | Upload a file |
| `ftp get` | `<node> <remote> <local>` | Download a file |
| `ftp generate` | `<path> <bytes>` | Create a test file of up to 32768 bytes |
| `ftp verify` | `<first> <second>` | Compare two local files |

Without a node, `ls`, `stat` and `mkdir` act on this node, without a
connection; `put` and `get` need another node. Listing `/` also shows the
read-only `boot` (firmware slots, see [firmware update](../fwu/README.md)) and
`hk` (housekeeping files) when they exist.

# Shell commands {#commands}

[TOC]

## Basics

Type commands at the shell prompt:

```text
kfsw:~$ status
```

Wait for `@READY` before using services. `help` lists the commands in the
build and `<command> -h` shows the syntax. Tab completes command and
subcommand names, but not arguments such as a node or a path. When more than
one name fits, Tab lists them one per line with their help, the same way
`<command> -h` does. A command with
the wrong number of arguments prints its usage:

```text
kfsw:~$ ftp generate
generate: wrong parameter count
generate - Create deterministic local data: generate <path> <bytes 0..32768>.
```

## Commands and build options

The shell needs `CONFIG_KFSW_DEBUG_SHELL`. Each command group depends on its
service:

| Command | Build option |
| --- | --- |
| `status`, `version`, `time`, `log` | always |
| `csp` | `CONFIG_KFSW_CSP` |
| `uart` | `CONFIG_KFSW_CSP_KISS_UART` |
| `param` | `CONFIG_KFSW_PARAM`; saving needs `CONFIG_KFSW_PARAM_PERSISTENCE` |
| `storage` | `CONFIG_KFSW_STORAGE` |
| `ftp` | `CONFIG_KFSW_FTP` |
| `event` | `CONFIG_KFSW_EVENT` |
| `journal` | `CONFIG_KFSW_JOURNAL` |
| `reboot` | `CONFIG_KFSW_COMMAND` and `CONFIG_REBOOT` |
| `hk` | `CONFIG_KFSW_HK` |
| `fbo` | `CONFIG_KFSW_FBO` |
| `fwu` | `CONFIG_KFSW_FWU` |
| `watchdog` | `CONFIG_KFSW_WATCHDOG` |
| `health` | `CONFIG_KFSW_HEALTH` |
| `gndwdt` | `CONFIG_KFSW_GNDWDT` |
| `resmon` | `CONFIG_KFSW_RESMON` |
| `uhf` | `CONFIG_KFSW_RADIO_UHF_SHELL` |
| `temp` | `CONFIG_KFSW_TEMP_EXAMPLE_SHELL` |
| `boton_test`, `test` | `CONFIG_KFSW_BOTON_TEST_SHELL` |

## Identity and time

| Command | Prints |
| --- | --- |
| `status` | Role, name, CSP node, board, hardware ID and uptime |
| `version` | K-FSW version, Zephyr version, board, SoC and hardware ID |
| `time` | Milliseconds and microseconds since boot |

```text
kfsw:~$ status
K-FSW status
Role: flight
Name: kfsw
CSP node: 1
board: native_sim/native/64
unit: 007f0101
uptime_ms: 10
```

Role and name are labels set in the build. `time` is time since boot; wall
time is in `csp clock`. An image is identified by its revision, the `git
describe` of the build; no compile date is reported, because the date a file
was compiled says little about the image it ends up in.

## Logging

`log test` prints one message at each level compiled into the image. Change
`log_level`, or `log_levels` for a single module, to filter them.

`log history` shows up to 32 recent retained messages when
`CONFIG_KFSW_LOG_HISTORY` is enabled. It includes sequence, uptime, module,
level and truncation status. To read them over CSP from the host, use
`./tools/kfsw-linux csp ... logs`, described in [communications](../communications/index.md#remote-text-logs-and-discovery).

## UHF radio

`uhf status` prints the radio implementation, the expected hardware and serial
settings, and the link state. With `CONFIG_KFSW_RADIO_UHF_CRYPTO`, `uhf connect`
starts new encrypted sessions with the configured peer.

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

`uhf status` doesn't talk to the modem. Traffic and errors are in `uart info`
and `csp interfaces`.

## Button and LED example

| Command | Arguments | Meaning |
| --- | --- | --- |
| `boton_test status` | none | Press count, last press and LED states |
| `test led` | `<green|blue|red> <on|off>` | Switch one LED |

```text
kfsw:~$ boton_test status
press_count: 0
last_press_s: 0
led_green: off
led_blue: off
led_red: off
debounce_ms: 30
```

`test led` and the LED parameters use the same module function. Holding the
button counts one press, and everything resets at boot.

`temp status` prints the cached die temperature of the temperature example
and its read counters.

## CSP

| Command | Arguments | Meaning |
| --- | --- | --- |
| `csp info` | none | Local address, identity, revision and free buffers |
| `csp ident` | `[node]` | Hostname, model, revision and clock |
| `csp interfaces` | none | Interfaces with addresses and packet, error and drop counters |
| `csp ifstat` | `<node> <interface>` | Remote interface packet/byte/error counters |
| `csp routes` | none | Route table |
| `csp ping` | `[node]` | Ping with CRC32 and a one-second timeout |
| `csp debug` | `[on\|off]` | Log every packet in and out |
| `csp clock` | `[set <utc>]` or `<node> [sync]` | Read or set wall time |
| `csp reboot` | `<node> <pin>` | Restart a node |

```text
kfsw:~$ csp routes
0/0 -> KISS direct
kfsw:~$ csp ping 2
CSP ping 2: success
rtt_ms: ...
```

`csp debug on` logs each packet's source and destination node and port,
priority, flags, size and interface, so the trace also lands in `log history`.
It is off by default and only affects the node where it is turned on.

```text
kfsw:~$ csp debug on
CSP packet trace: on
kfsw:~$ csp ping 2
[INFO] OUT: S 33, D 2, Dp 1, Sp 17, Pr 2, Fl 0x01, Sz 10 VIA: CAN (2), Tms 51060
[INFO] INP: S 2, D 33, Dp 17, Sp 1, Pr 2, Fl 0x01, Sz 14 VIA: CAN, Tms 51120
CSP ping 2: success
rtt_ms: 60.000
```

With several links, `csp routes` shows the interface and next hop of each
route:

```text
kfsw:~$ csp routes
10/14 -> KISS_1 direct
11/14 -> KISS_2 via 11
```

Routes are set at build time and can't be changed from the shell. A node that
does not answer is logged as a warning, `[WARNING] csp ping: node 5 did not
answer`, and kept in `log history`; the same goes for every request to another
node, `param`, `status`, `ftp` and the rest. No answer can mean no peer, a wrong
address or route, framing errors or no free buffers; check `csp interfaces`,
`csp routes` and `uart info`.

### Restarting a node

`csp reboot <node> <pin>` restarts a node if the pin matches, and `reboot <pin>`
restarts this one:

```text
kfsw:~$ csp reboot 2 1234
reboot: denied, wrong pin

kfsw:~$ csp reboot 2 0000
node: 2
rebooting in 500 ms
```

The pin is `reboot_pin` in the system table: `0000` by default, persistent,
and settable from the ground. It is text, so `0007` doesn't match `7`. The pin
is sent in clear text and only protects against rebooting the wrong node by
mistake.

After a restart the boot table shows why the previous run ended:

```text
kfsw:~$ param table 2 32
32  0x30  last_reason      u8   r  1          commanded
32  0x34  last_detail      x32  r  0x00000000 the faulting address, for a fault
32  0x38  last_uptime_ms   u32  r  5427214
```

All three at zero means no valid reset note was found. Power loss is one
possible cause; a reset before a note was written gives the same result.
Check the hardware reset cause as well.

## UART/KISS

| Command | Arguments | Meaning |
| --- | --- | --- |
| `uart info` | none | Each CSP UART with baud, state, KISS name, address and counters |
| `uart test` | `[node]` | Check that the route uses a UART/KISS link, then ping with a 128-byte payload |

Give the node to test one link out of several, for example `uart test 10` and
`uart test 11`. On the NUCLEO these commands run on the ST-LINK console and the
packets leave on USART3; don't connect the shell terminal to the CSP UART.

## Parameters

| Command | Arguments | Meaning |
| --- | --- | --- |
| `param tables` | `[node]` | The tables a node carries, without values |
| `param table` | `[node] <id>` | The parameters of one table, with values |
| `param list` | `[node]` | Every parameter of a node, with values |
| `param get` | `[node] <name>` | Read a value |
| `param set` | `[node] <name> <value>` | Write a value |

Three views of the same parameters, from the widest to the most detailed:
`tables` says which tables exist, `table` shows one, `list` shows them all. `layer` says which part of K-FSW defines the table, `kept` how many of its
values are saved, and `holds` what is in it. Table names are not sent over the
link, so for another node the names and descriptions come from this build,
whose table numbers are the same.

```text
kfsw:~$ param tables
 id  layer    name          params  kept  holds
---  -------  ------------  ------  ----  -----
  1  core     board             11     0  Node identity and what the board carries
  2  core     system             3     3  Boot delay, report period, reboot pin
  3  core     telemetry          5     0  Uptime, storage and CSP buffers
  4  core     csp               15     0  CSP counters and the route table
  5  core     storage            4     0  Filesystem size, free space, mount
 25  service  log                5     2  Log levels, colour and counters
```

```text
kfsw:~$ param list
table       addr  name                              type    mode  value
----------  ----  --------------------------------  ------  ----  -----
board       0x00  node_id                           u16     r     1
system      0x00  boot_delay_ms                     u16     wpb   0
system      0x02  app_report_ms                     u16     wp    1000
telemetry   0x00  uptime_s                          u32     r     0
log         0x00  log_level                         u8      wp    1
```

Mode letters: `r` read-only, `w` writable, `p` saved in the snapshot, `b`
applied at the next boot. Strings are printed quoted and arrays as lists.
Input is parsed with the parameter's type; overflow, negative unsigned values
and bad numbers are rejected.

`echo_enabled` makes the console repeat what it receives. It is off by
default:

```text
kfsw:~$ param set echo_enabled 1
kfsw:~$ status          <- repeated by the shell
K-FSW status
```

### Remote nodes

With `CONFIG_KFSW_PARAM_CSP`, put the node before the other arguments:

```text
kfsw:~$ param get 2 log_level
2:log_level = 1
kfsw:~$ param set 2 log_level 2
2:log_level = 2
kfsw:~$ param table 2 25
```

The node is a decimal number. Remote listings show the table number instead
of its name, because table names are not sent over the link.

### Saving

| Command | Effect |
| --- | --- |
| `param save` | Write all persistent values to the snapshot |
| `param persist <table>` | Write the snapshot and show that table's share |
| `param load` | Apply the saved snapshot |
| `param defaults` | Reset persistent values to their defaults, in RAM only |
| `param clear` | Delete the snapshot; RAM is unchanged |

These act on the local node. With `param_autosave` on, a change to a
persistent value is saved without `param save`. See @ref services.

## Storage

| Command | Arguments | Meaning |
| --- | --- | --- |
| `storage info` | none | Filesystem, backend, mount point, state, total and free bytes |
| `storage test` | none | Create, write, read, overwrite and delete a test file |
| `storage test write` | `<value>` | Write the persistence test value |
| `storage test read` | `<value>` | Read and compare the persistence test value |

```text
kfsw:~$ storage info
K-FSW storage
filesystem: LittleFS
backend: flash-controller@0
mount_point: /kfsw
ready: yes
total_bytes: 262144
free_bytes: 237568
```

`storage test` writes to flash. `storage info` only reads.

## File transfer

Paths are virtual and rooted at `/kfsw/ftp` on the node.

| Command | Arguments | Meaning |
| --- | --- | --- |
| `ftp <node> ls` | `[directory]` | List a directory; `list` also works |
| `ftp <node> stat` | `<path>` | Type, size and CRC |
| `ftp <node> mkdir` | `<directory>` | Create a directory |
| `ftp <node> put` | `<local> <remote>` | Upload a file |
| `ftp <node> get` | `<remote> <local>` | Download a file |
| `ftp generate` | `<path> <bytes>` | Create a test file of up to 32768 bytes |
| `ftp verify` | `<first> <second>` | Compare two local files |

The verb can also go first, `ftp put <node> ...`, which is the form Tab
completion shows. `ls`, `stat` and `mkdir` work on the node's own address
without a connection; `put` and `get` need another node.

```text
kfsw:~$ ftp generate /build/sample.bin 1024
kfsw:~$ ftp mkdir 2 /exchange
kfsw:~$ ftp put 2 /build/sample.bin /exchange/sample.bin
kfsw:~$ ftp stat 2 /exchange/sample.bin
kfsw:~$ ftp get 2 /exchange/sample.bin /build/returned.bin
kfsw:~$ ftp verify /build/sample.bin /build/returned.bin
```

Paths must start with `/` and can't contain `..` or empty components.

## Another node

Groups that can ask another node take the node first, as `param` does:
`status 2`, `event stats 2`, `journal tail 2 0`, `hk period 2 0 1000`. A node
gets 3 seconds to answer; one that does not is logged as a warning. The reply
comes back one field per line, after the node it came from:

```text
kfsw:~$ status 2
node: 2
uptime_ms: 4140
storage: ready
free_bytes: 12288
```

| Command | Arguments | Meaning |
| --- | --- | --- |
| `status` | `[node]` | Uptime and storage of a node |
| `event stats` | `[node]` | Event record counters |
| `event tail` | `[node] <age>` | One event record, newest is 0 |
| `journal stats` | `[node]` | Persistent journal state |
| `journal tail` | `[node] <age>` | One saved event, newest is 0 |
| `journal time` | `[node] <age>` | When a saved event happened |
| `hk define`, `hk period`, `hk clear` | `[node] ...` | Set up a node's housekeeping |
| `reboot` | `<pin>` | Restart this node |
| `csp reboot` | `<node> <pin>` | Restart another node |
| `gndwdt feed`, `gndwdt show` | `<node>` | Its ground watchdog |

Underneath, each of these is a command of the command service, sent over CSP
port 11; the commands are the wire protocol a ground tool speaks. A node only
asks another for commands it knows itself, so both run the same IDs, listed in
the table below. A remote request that changes the node can end in `--retry`,
for example `csp reboot 2 0000 --retry`; see Scripts, below.

### Identifiers

Four kinds of number are part of the wire contract: two nodes must agree on
each, and none is reused for something else once it has been given out.

**Command IDs.** Composition commands are defined in
`app/src/commands/command_definitions.c`; a command that belongs to a service
is defined by that service and carries its ID in its own header.

| ID | Command | Defined in |
| --- | --- | --- |
| 1 to 3 | `noop`, `info`, `reboot` | `k-fsw` composition |
| 4, 5 | `event_stats`, `event_tail` | `k-fsw` composition |
| 6 to 8 | `hk_define`, `hk_period`, `hk_clear` | `k-fsw` composition |
| 9 to 11 | `journal_stats`, `journal_tail`, `journal_time` | `k-fsw` composition |
| 12 to 15 | free | — |
| 16 | `ground_wtd` | `kfsw-services`, `gndwdt.h` |

**CSP ports.** 0 is management (CMP) and 1 ping, both from libcsp. K-FSW
serves 9 file transfer, 10 parameter values, 11 commands, 12 parameter
descriptors, 13 FWU lite, 14 housekeeping, 15 housekeeping beacons on the
receiving node, and 16 log history. Each has a Kconfig option; see
@ref communications.

**Parameter tables.** 1 to 24 are core (1 `board`, 2 `system`, 3
`telemetry`, 4 `csp`, 5 `storage`), 25 to 49 services (25 `log` through 36
`resmon`, listed by `param tables`) and 50 to 99 modules (50 `radio-uhf`, 51
`temp_example`, 67 `hw_test`).

**Node addresses.** CSP v2 addresses are 1 to 16383. Flight nodes use 1 to
15; ground roles start at 16, which `tools/k-ground` enforces. The reference
ground station uses 16 for the gateway and 19 for the operator node.

The name is looked up in the local registry before the request is sent, so
both nodes need the same command IDs. `reboot` and `ground_wtd` change the node.
Remote commands need `CONFIG_KFSW_COMMAND_CSP` (port 11). The command service
does not authenticate callers. Radio encryption can protect that link;
other interfaces need their own access policy.

## Events

| Command | Meaning |
| --- | --- |
| `event list` | Held records, oldest first |
| `event stats [node]` | Held, capacity, recorded, overwritten and rejected counts |
| `event tail [node] <age>` | One record by age, newest is 0 |
| `event clear` | Discard held records; counters are kept |

```text
kfsw:~$ event list
     SEQ    TIME_MS SOURCE   SEVERITY    ID PAYLOAD
       0          0 boot     info         1 0000000800
Events listed: 1
```

Payloads are printed as bytes; their layout is in the producer's header. A gap
in `SEQ` means records were overwritten, and `event stats` shows how many. The
record is lost at reset. For another node:

```text
kfsw:~$ event tail 2 0
node: 2
seq: 8
t_ms: 8120
src: ftp
id: 1
sev: 0
data: 0002000001000ce9d363
```

## Housekeeping

| Command | Arguments | Meaning |
| --- | --- | --- |
| `hk define` | `[node] <report> [node:]table:offset ...` | Set what a report collects |
| `hk clear` | `[node] <report>` | Delete a report |
| `hk show` | none | Reports and counters |
| `hk collect` | `<report>` | Collect now |
| `hk get` | `<report> [count]` | Print collected samples |
| `hk period` | `[node] <report> <ms>` | Collect periodically, 0 to stop |
| `hk store` | `<report> <ms>` | Also write samples to a file, 0 to stop |
| `hk store_clear` | `<report>` | Stop storing and delete the file |
| `hk beacon` | `<report> <node> <ms>` | Send the latest sample periodically, 0 to stop |
| `hk save` | none | Save the report settings |

A leading node sets up that node's reports: `hk define 2 0 3:0 3:8` asks node 2
to collect its own uptime and free storage as report 0. An entry always holds a
`:`, so the node is told apart from the report. See @ref ground for Yamcs and
the ground bridge.

## File based operations

| Command | Arguments | Meaning |
| --- | --- | --- |
| `fbo run` | `<name>` | Run a procedure file |
| `fbo stop` | none | Stop the running procedure |
| `fbo status` | none | Procedure, current line and counters |

## Firmware update

```text
fwu status               state, slot geometry and checksums
fwu begin <size> <crc>   start a transfer by hand
fwu finish               verify and offer the image to the bootloader
fwu abort                abandon a transfer and erase the slot
fwu send <node> <path>   send an image block by block
fwu flash <node>         ask a node to boot the image it received
```

`send` and `flash` need `CONFIG_KFSW_FWU_LITE_CSP`. `fwu send` reports how many
blocks had to be resent. Check link counters if retries increase.

After a transfer, check `swap_scheduled` in `fwu status`. If it is not set,
MCUboot has nothing to swap and boots the old image. See
@ref firmware_update.

## Watchdog and health

What each of the three watchdogs proves, and what feeds it, is in
@ref services, under Watchdogs.

```text
gndwdt show [node]                 ground watchdog countdown, here or on a node
gndwdt feed <node>                 feed another node's ground watchdog
gndwdt on | off                    arm or disarm the local countdown
gndwdt timeout <seconds>           silence allowed before a reset
```

```text
watchdog status                    configuration and activity
watchdog feed                      feed once
watchdog starve confirm            stop feeding; the board resets
health status                      whether the watchdog is being fed
health list                        watched components and deadlines
health report <handle>             report a component as alive
health watch <name> <ms> confirm   watch a component that never reports
```

The `watchdog` commands need `CONFIG_KFSW_WATCHDOG` and a `kfsw,watchdog`
devicetree property. `watchdog status` shows the state (`unconfigured`,
`configured`, `running` or `starved`), the timeout, the feed interval, the
number of feeds and the time since the last one.

`watchdog starve confirm` can't be undone, since the STM32 independent
watchdog can't be disarmed; the board resets within one timeout. `health
watch` also ends in a reset once the deadline passes. The next boot marker
shows the cause:

```text
@BOOT sw=v1.0.1 board=nucleo_l496zg/stm32l496xx unit=203037324d46500c0010001f reset=0x00000011 reset_rc=0 reset_cause=watchdog
```

The raw mask is printed as well because several causes can be latched at once;
`0x11` is the reset pin and the watchdog.

## Scripts

Remote requests are not retried. Ending one with `--retry` sends it with a
ticket that suppresses duplicates within that invocation, when
`CONFIG_KFSW_COMMAND_RETRY` is enabled.
A new invocation is a new operation. A timeout can mean the reply was lost
after the command ran, so check the state before sending it again.

Wait for `@READY`, then send one command at a time and wait for the prompt.
Use the C APIs or CSP services when a script needs a stable format.
