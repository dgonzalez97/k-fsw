# Shell commands {#commands}

[TOC]

## Basics

Type commands at the shell prompt:

```text
kfsw:~$ status
```

Wait for `@READY` before using services. `help` lists the commands in the
build and `<command> -h` shows the syntax. Tab completes command and
subcommand names, but not arguments such as a node or a path. A command with
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
| `cmd` | `CONFIG_KFSW_COMMAND` |
| `event` | `CONFIG_KFSW_EVENT` |
| `hk` | `CONFIG_KFSW_HK` |
| `fbo` | `CONFIG_KFSW_FBO` |
| `fwu` | `CONFIG_KFSW_FWU` |
| `watchdog` | `CONFIG_KFSW_WATCHDOG` |
| `health` | `CONFIG_KFSW_HEALTH` |
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
time is in `csp clock`.

## Logging

`log test` prints one message at each level compiled into the image. Change
`log_level`, or `log_levels` for a single module, to filter them.

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
| `csp info` | none | Local address, identity, build date and free buffers |
| `csp ident` | `[node]` | Hostname, model, revision, build date and clock |
| `csp interfaces` | none | Interfaces with addresses and packet, error and drop counters |
| `csp routes` | none | Route table |
| `csp ping` | `[node]` | Ping with CRC32 and a one-second timeout |
| `csp debug` | `[on\|off]` | Print every packet in and out |
| `csp clock` | `[set <utc>]` or `<node> [sync]` | Read or set wall time |
| `csp reboot` | `<node> <pin>` | Restart a node |

```text
kfsw:~$ csp routes
0/0 -> KISS direct
kfsw:~$ csp ping 2
CSP ping 2: success, rtt_ms=...
```

`csp debug on` prints each packet's source and destination node and port,
priority, flags, size and interface. It is off by default and only affects the
node where it is turned on.

```text
kfsw:~$ csp debug on
CSP packet trace: on
kfsw:~$ csp ping 2
[DEBUG] OUT: S 33, D 2, Dp 1, Sp 17, Pr 2, Fl 0x01, Sz 10 VIA: CAN (2), Tms 51060
[DEBUG] INP: S 2, D 33, Dp 17, Sp 1, Pr 2, Fl 0x01, Sz 14 VIA: CAN, Tms 51120
CSP ping 2: success, rtt_ms=60
```

With several links, `csp routes` shows the interface and next hop of each
route:

```text
kfsw:~$ csp routes
10/14 -> KISS_1 direct
11/14 -> KISS_2 via 11
```

Routes are set at build time and can't be changed from the shell. A ping
timeout can mean no peer, a wrong address or route, framing errors or no free
buffers; check `csp interfaces`, `csp routes` and `uart info`.

### Restarting a node

`csp reboot <node> <pin>` restarts a node if the pin matches:

```text
kfsw:~$ csp reboot 2 1234
reboot node=2: denied wrong pin

kfsw:~$ csp reboot 2 0000
reboot node=2: OK rebooting in 500 ms
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

All three at zero means the node lost power.

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
| `param tables` | none | Local tables with ID, band, size and saved values |
| `param tablelist` | `[node]` | Tables of a node |
| `param list` | `[node]` | All parameters |
| `param table` | `[node] <table>` | One table |
| `param get` | `[node] <name>` | Read a value |
| `param set` | `[node] <name> <value>` | Write a value |

```text
kfsw:~$ param tables
 id  band     name        params    kept
---  -------  ----------  ------  ------
  1  core     board           11       0
  2  core     system           3       3
  3  core     telemetry        5       0
  4  core     csp              8       0
  5  core     storage          4       0
 25  service  log              5       2
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
kfsw:~$ ftp 2 mkdir /exchange
kfsw:~$ ftp put 2 /build/sample.bin /exchange/sample.bin
kfsw:~$ ftp stat 2 /exchange/sample.bin
kfsw:~$ ftp get 2 /exchange/sample.bin /build/returned.bin
kfsw:~$ ftp verify /build/sample.bin /build/returned.bin
```

Paths must start with `/` and can't contain `..` or empty components.

## Commands

| Command | Arguments | Meaning |
| --- | --- | --- |
| `cmd list` | none | Registered commands with ID, arguments and description |
| `cmd <name>` | `[arguments]` | Run a command on this node |
| `cmd <node> <name>` | `[arguments]` | Run a command on another node over CSP |

```text
kfsw:~$ cmd list
 ID NAME         ARGS   DESCRIPTION
  1 noop         0 args Round trip with no effect.
  2 info         0 args Report uptime and storage state.
  4 event_stats  0 args Report event record counters.
  5 event_tail   1 arg  Read one recorded event by age, newest is 0.
  6 hk_define    2 args Name what a report collects: <report> "[node:]table:offset ...".
  7 hk_period    2 args Collect repeatedly: hk_period <report> <ms>, 0 to stop.
  8 hk_clear     1 arg  Forget a report: hk_clear <report>.
  3 reboot       1 arg  [mutating] Reset this node after a short delay: reboot <pin>.

kfsw:~$ cmd 2 info
info node=2: OK uptime_ms=4140 storage=ready free_bytes=12288
```

The name is looked up in the local registry before the request is sent, so
both nodes need the same command IDs. `[mutating]` commands change the node.
Remote commands need `CONFIG_KFSW_COMMAND_CSP` (port 11). There is no
authentication.

## Events

| Command | Meaning |
| --- | --- |
| `event list` | Held records, oldest first |
| `event stats` | Held, capacity, recorded, overwritten and rejected counts |
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
kfsw:~$ cmd 2 event_stats
event_stats node=2: OK held=9/32 recorded=9 overwritten=0 rejected=0
kfsw:~$ cmd 2 event_tail 0
event_tail node=2: OK seq=8 t=8120ms ftp/1 sev=0 0002000001000ce9d363
```

## Housekeeping

| Command | Arguments | Meaning |
| --- | --- | --- |
| `hk define` | `<report> [node:]table:offset ...` | Set what a report collects |
| `hk clear` | `<report>` | Delete a report |
| `hk show` | none | Reports and counters |
| `hk collect` | `<report>` | Collect now |
| `hk get` | `<report> [count]` | Print collected samples |
| `hk period` | `<report> <ms>` | Collect periodically, 0 to stop |
| `hk store` | `<report> <ms>` | Also write samples to a file, 0 to stop |
| `hk store_clear` | `<report>` | Stop storing and delete the file |
| `hk beacon` | `<report> <node> <ms>` | Send the latest sample periodically, 0 to stop |
| `hk save` | none | Save the report settings |

See @ref ground for Yamcs and the ground bridge.

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
blocks had to be resent; a growing number means the link is getting worse.

After a transfer, check `swap_scheduled` in `fwu status`. If it is not set,
MCUboot has nothing to swap and boots the old image. See
@ref firmware_update.

## Watchdog and health

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

Commands are not retried. A timeout can mean the reply was lost after the
command ran, so check the state before sending it again. Wait for `@READY`,
send one command, check its output and wait for the prompt before the next
one. Don't rely on the spacing of shell output; use the C APIs or the CSP
services when a script needs a stable format.
