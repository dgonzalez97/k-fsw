# Services and storage {#services}

[TOC]

## Services

`kfsw-services` provides boot markers, logging, parameters, persistence,
files, commands, events, health, housekeeping, file based operations, and
firmware updates. The application selects and starts them.

Storage is in `kfsw-platform`. Network services use the router and interfaces
in `kfsw-comms`. For housekeeping and Yamcs, see @ref ground. For uploads and
boot recovery, see @ref firmware_update. The API is in @ref kfsw_services.

## Boot and readiness markers

At startup the boot service reads and clears the reset cause and prints:

```text
@SERVICES ok failures=0
@BOOT sw=<image version> board=<board target> unit=<hardware id> reset=<flags> reset_rc=<result> reset_cause=<name>
@SOURCE <release commit or development>
@READY uptime_ms=<milliseconds>
```

The image version is `git describe` of the `k-fsw` repository when the build
is configured, and `version` prints the same string. `@SERVICES degraded
failures=N` means some services failed to start. `@READY` marks the end of
startup; release builds set the version and source commit explicitly, see
@ref development.

### What was compiled into an image

`boot_image` carries `git describe` of `k-fsw` alone, so two images built from
the same tag with different dependency pins report the same version. The image
also carries the short revision of every repository that went into it:

```text
kfsw:~$ version
K-FSW: v1.0.1-26-g1ba9630
Revisions: app:1ba96309fb plat:31818d2c37 svc:919c4c43ba comms:d35abd4986 mod:b476a0c9ed
```

The same string is `boot_revisions` in table 32, so a ground station reads it
like any other parameter. A repository that had uncommitted changes when it was
built gets a trailing `+`, which is how a bench image is told apart from a
built one.

## Housekeeping

Reports collect local and remote parameters. A periodic report keeps its
cadence: a slow collection skips the missed slots instead of catching up.
`hk show` reports attempts and missed slots.

Remote reads in one collection share one time budget. Local sample callbacks
and storage drivers need their own time limits. Redefining a report cancels a
collection in progress.

With persistence enabled, report settings are saved on every change. If a save
fails, the shell says the change is only in RAM; retry with `hk save`. A
rejected settings file is kept until a save replaces it. Restoring settings
keeps the existing sample files and their sequence numbers.

## File based operations

`fbo run <name>` runs the commands in a procedure file, `fbo stop` ends the
run, and `fbo status` shows what it did. File size, line length and scanned
bytes are limited, and comments count toward the scan limit. An I/O error
stops the procedure.

```text
on-error continue
noop
info
on-error stop
wait 1
if-event 9 9 skip
noop
```

`on-error` chooses whether a failed line stops the run, `wait` pauses, and
`if-event <source> <id> skip` skips the next line unless that event is in the
event record. There are no loops or jumps.

## Logging

Messages have four levels: DEBUG, INFO, WARNING and ERROR.
`CONFIG_KFSW_LOG_MIN_LEVEL` removes the lower levels from the build. At
runtime `log_level` sets a global level and `log_levels` one per module; a
message has to pass both. An invalid level is rejected.

Each message is printed as one console line, with newlines replaced by
spaces. Lines are coloured by level: errors red, warnings yellow, info white
and debug dim. The colour codes wrap the whole line, so `[LEVEL] message`
stays intact for scripts. `log_color` turns colour off.

## Parameters

Parameters are named, typed values grouped in tables. Each service or module
defines its own table, with the storage, validators and change callbacks. The
application passes the enabled tables to `kfsw_param_init()`, which checks
them and builds a sorted index. Persistence and the CSP adapter use the same
index.

```text
KFSW_PARAM              local tables and API
KFSW_PARAM_PERSISTENCE  needs KFSW_PARAM and KFSW_STORAGE
KFSW_PARAM_CSP          needs KFSW_PARAM and KFSW_CSP
KFSW_FTP                needs KFSW_STORAGE and KFSW_CSP
```

`KFSW_PARAM` does not need CSP; `tests/param-local-smoke.sh` builds a
local-only composition.

### Tables

A parameter is addressed by table and offset. Table numbers are split in
bands:

| Band | Used by |
| --- | --- |
| 0 | Reserved, never valid |
| 1-24 | Application, platform and comms |
| 25-49 | Services, one table each |
| 50-99 | Modules |
| 100-255 | Free for mission payloads |

On the wire the table is the high byte and the offset the low byte. Offsets
are unique inside a table. Names are unique on the node and up to
`KFSW_PARAM_NAME_MAX` (32) characters; a longer name is refused at
registration.

| ID | Band | Name | Source |
| --- | --- | --- | --- |
| 1 | core | `board` | `k-fsw/app/src/parameters/board_table.c` |
| 2 | core | `system` | `k-fsw/app/src/parameters/system_table.c` |
| 3 | core | `telemetry` | `k-fsw/app/src/parameters/telemetry_table.c` |
| 4 | core | `csp` | `k-fsw/app/src/parameters/csp_table.c` |
| 5 | core | `storage` | `k-fsw/app/src/parameters/storage_table.c` |
| 6 | core | `watchdog` | `k-fsw/app/src/parameters/watchdog_table.c` |
| 24 | core | `test` | `k-fsw/tests/support/parameter_definitions.c` |
| 25 | service | `log` | `kfsw-services/src/log.c` |
| 26 | service | `param` | `kfsw-services/src/param-parameters/` |
| 27 | service | `event` | `kfsw-services/src/event-parameters/` |
| 28 | service | `command` | `kfsw-services/src/command-parameters/` |
| 29 | service | `ftp` | `kfsw-services/src/ftp-parameters/` |
| 30 | service | `fwu` | `kfsw-services/src/fwu-parameters/` |
| 31 | service | `health` | `kfsw-services/src/health-parameters/` |
| 32 | service | `boot` | `kfsw-services/src/boot-parameters/` |
| 33 | service | `hk` | `kfsw-services/src/hk-parameters/` |
| 34 | service | `fbo` | `kfsw-services/src/fbo-parameters/` |
| 50 | module | `radio_uhf` | `kfsw-modules/radio-uhf/parameters/` |
| 51 | module | `temp_example` | `kfsw-modules/temperature-sensor-example/parameters/` |
| 67 | module | `hw_test` | `kfsw-modules/boton-test/` |

A table is only registered when its service or module is enabled. The `fwu`
table is read-only. `health_interval_ms` is checked against the watchdog
timing before it is stored. Core tables are in the application because the
platform and comms layers can't depend on the parameter service.

### Write modes

The `mode` column of `param list` comes from each definition:

| Letter | Meaning |
| --- | --- |
| `r` | Read-only |
| `w` | Writable |
| `p` | Saved in the snapshot |
| `b` | Applied at the next boot |

`wp` is saved and applied now, `wpb` is saved and applied at the next boot,
and `w` is applied now and lost at reset. A definition with a change callback
is applied immediately. Writing a `b` parameter prints that a reboot is
needed.

### Strings and arrays

A `KFSW_PARAM_STRING` holds up to `CONFIG_KFSW_PARAM_STRING_MAX` bytes (104 by
default) including the terminator, and a longer write is refused. A
`KFSW_PARAM_DATA` array is always written whole and validated as a whole.
Reads and writes use caller buffers; nothing is allocated.

### Sampled values

A definition can have a `sample` callback that refreshes the value just before
it is read. The CSP server calls it before answering a remote read as well.
Sampling runs under the table lock, so the callback must not call the
parameter API.

A remote write to a sampled parameter is applied from the stored value
(`kfsw_param_read_stored_entry()`) without sampling again, so the new value is
not overwritten.

### Console echo and colour

`echo_enabled` in table 28 is off by default, so a scripted session doesn't
show each command twice. Turn it on to see what the console received, for
example when checking tab completion. The shell prompt is jade; its colour is
set at runtime because Kconfig strings can't hold escape sequences.

### Build-time settings

`ftp_root` is read-only because the path resolver uses a compile-time string.
`ftp_chunk_size` can only be lowered, since the transfer buffer is sized at
build time. `fwu_lite_block_size` is read-only for the same reason. The radio
table reports `uhf_link_state` as `unknown` because the module does not query
the modem.

### Remote parameters

`CONFIG_KFSW_PARAM_CSP` adds `parameter_csp.c` and the parts of
[libparam](https://github.com/spaceinventor/libparam) it needs. The local
tables are served on CSP port 10 (values) and 12 (descriptors), and the client
caches the descriptors of one remote node.

```text
kfsw:~$ param get 2 log_level
kfsw:~$ param set 2 log_level 2
kfsw:~$ param list 2
```

A named read asks the node for that one descriptor and then the value. A node
that doesn't answer the lookup is read by downloading its whole descriptor
list. The list is downloaded one indexed descriptor at a time and checked with
a table CRC, so a failed download leaves no partial cache.
`CONFIG_KFSW_PARAM_LIST_TIMEOUT_MS` (10 s) limits the download; raise it on
nodes that list parameters over a slow radio.

The cache holds `CONFIG_KFSW_PARAM_REMOTE_POOL_SIZE` descriptors. Value requests
are handled by a worker thread; when its queue is full the request is dropped
and `param_requests_dropped` increases.

Each node validates remote writes with its own validators. An invalid remote
value is rejected and the parameter goes back to its compiled default.

### Defaults, save and load

At boot every value starts from its compiled default, then the snapshot is
restored. `param set` changes RAM and runs the change callback. With
`param_autosave` on (the default), an accepted change to a persistent value
also writes the snapshot.

| Command | RAM | Saved snapshot |
| --- | --- | --- |
| `param set <name> <value>` | Changed | Written for persistent values when autosave is on |
| `param save` | Unchanged | Replaced with the persistent values |
| `param persist <table>` | Unchanged | Replaced; prints that table's share |
| `param load` | Updated from valid entries | Unchanged |
| `param defaults` | Persistent values back to defaults | Unchanged |
| `param clear` | Unchanged | Deleted |

## Parameter persistence

The snapshot is `/kfsw/params/parameters.dat`, written through
`/kfsw/params/parameters.tmp`. It has a 20-byte header (magic `KPAR`, version
1, sizes, entry count and CRC32) followed by one entry per persistent value:
name, type, length and the big-endian value. It is limited to 2048 bytes and
64 entries; `param_persist_bytes` and `param_persist_max_bytes` in table 26
show how much is used.

Loading checks the size, structure and CRC before anything is applied. Unknown
names and entries with a different type are skipped, so another image version
keeps the values it understands. A bad snapshot is logged, the compiled
defaults stay, and startup continues. The filesystem is not reformatted.

A save writes and syncs the temporary file and then renames it over the active
file. If a step fails, the temporary file is removed and the previous snapshot
stays. The CRC catches corruption; it is not authentication.

## Storage

K-FSW mounts one [LittleFS](https://github.com/littlefs-project/littlefs)
volume at `/kfsw`. The platform layer handles init, mount, unmount and
capacity. Once it is mounted, services use the normal Zephyr
[filesystem API](https://docs.zephyrproject.org/4.4.0/services/file_system/index.html).

The partition is selected with the `kfsw,storage-partition` devicetree
property. The NUCLEO-L496ZG uses the last 64 KiB of its 1 MiB flash:

```text
0x00000000                     0x000F0000         0x00100000
|------------------------------|------------------|
| application, 960 KiB         | LittleFS, 64 KiB |
|------------------------------|------------------|
```

`CONFIG_USE_DT_CODE_PARTITION=y` keeps the application out of the storage
region. native_sim uses a 256 KiB partition backed by a host file,
`build/linux/kfsw-storage.bin` when started with the runner. The FRDM-K64F and
Pico W profiles have no storage.

### Mount and format

The first mount uses `FS_MOUNT_FLAG_NO_FORMAT`. If LittleFS reports the
partition as corrupt (`EFAULT`), the whole partition is scanned. A fully erased
partition is formatted and mounted; anything else is reported as an error and
left as it is. Services that need storage report their own errors when it is
not mounted.

## File transfer

K-FSW FTP is the project's own protocol and is not compatible with Internet
FTP, FTPS, SFTP or TFTP. It supports list, stat, mkdir, put and get. The server
listens on CSP port 9 and requires RDP and CRC32. Each message has a 24-byte
header with the request ID, offset, total size and file CRC.

Paths are virtual and rooted at `/kfsw/ftp` on each node:

```text
FTP path              Zephyr path
/build/sample.bin     /kfsw/ftp/build/sample.bin
/exchange/result.dat  /kfsw/ftp/exchange/result.dat
```

Paths are up to 96 bytes. Relative paths, empty components, `.` and `..`,
backslashes, control characters and embedded NULs are rejected. The sandbox is
not access control. `/hk` is a second, read-only root with the housekeeping
sample files.

### The local node

`list`, `stat` and `mkdir` addressed to the node's own CSP address run
directly on local storage, without a connection or a route. They need storage
mounted and the service started, otherwise they return `-EACCES`. `put` and
`get` need two nodes and return `-ENOTSUP` for the local address.

### Code layout

```text
ftp_client.c, ftp_server.c   requests and responses
ftp_transfer.c               the send and receive loops
ftp_link.h                   transport interface
ftp_link_csp.c               CSP with RDP and CRC32
ftp_protocol.c               wire codec and path checks
ftp_store.c                  file CRC, temporary files and rename
```

Only `ftp_link_csp.c` includes libcsp. A received frame points into the
transport buffer until `kfsw_ftp_link_release()` is called. RDP handles
ordering and retransmission, so FTP has no retry layer of its own; the offset
in each data message is checked so a stray or repeated packet can't advance
the write.

### Transfers

File data is sent in chunks of up to 192 bytes. The sender gives the total size
and IEEE CRC32, and the receiver checks both before it commits the file.

```text
client                              server
PUT (path, size, CRC32)       ->
                              <-    PUT_READY
DATA (offset 0)               ->
DATA (next offset)            ->    writes <path>.part
                              <-    PUT_RESULT after sync, check and rename
```

A download is the same in the other direction: GET, GET_INFO, DATA and
GET_RESULT. The receiving side always writes `<path>.part`, syncs it, checks
it and renames it. A failed transfer removes the partial file and leaves an
existing file alone.

The server has one acceptor and one worker and answers `busy` to a second
connection. Client calls share one static workspace behind a mutex. Each
receive waits up to `ftp_timeout_ms` (15 s by default). `ftp generate` is
limited to 32768 bytes; transfers are limited by the 32-bit size field, free
space and the link.

## Command service

`CONFIG_KFSW_COMMAND` enables the command registry used by the `cmd` shell
command and by remote callers on CSP port 11. A command has a name, a numeric
ID, up to four typed arguments and a result.

```text
shell    cmd info       found by name
ground   ID 2, port 11  found by ID
             |
   same definition, validation and handler
```

Commands are registered at build time as sets and the registry is fixed at
startup. Duplicate IDs or names, missing handlers and too many arguments are
rejected. Handlers run on the command thread, one at a time, never in a CSP
receive context.

A message has a 12-byte big-endian header (version, opcode, status, argument
count, command ID, request ID and payload size) followed by type-length-value
arguments. Every length is checked before use, and a message always fits one
CSP packet.

There is no authentication. The request carries the source node and an
authentication flag that is always false. See @ref kfsw_services_command.

A handler runs to completion and returns a status and an optional short text.
Requests are not deduplicated, so check the node state before sending a
command again after a lost reply. Remote parameters use the parameter service,
not commands.

## Ground watchdog

A node can be running, healthy by its own measure, and still unreachable: a
route that no longer resolves, a link left in the wrong configuration, a
service that stopped answering. Health monitoring cannot see any of that,
because every component inside the node is reporting normally.

The ground watchdog resets the node when it hears nothing from anyone. Contact
is any packet the CSP router accepts from another node, counted through a hook
in `kfsw-comms`, so no service has to report anything and a packet to any port
holds the countdown open. Packets this node sends to itself do not count.

```text
packet from another node -> router -> contact -> countdown restarts
no packet for gndwdt_timeout_s   ->   event, log line, then a reset
```

Table 35 carries the timer:

| Parameter | Access | Meaning |
| --- | --- | --- |
| `gndwdt_enabled` | rw | Whether the countdown is armed |
| `gndwdt_timeout_s` | rw | Silence allowed, one day by default |
| `gndwdt_since_s` | r | Seconds since the last contact |
| `gndwdt_contacts` | r | Packets counted as contact |
| `gndwdt_expiries` | r | Times the timeout passed |
| `gndwdt_last_node` | r | Node of the most recent contact |
| `gndwdt_running` | r | Whether the service was started |

Writing `gndwdt_timeout_s` restarts the countdown, so raising it never resets a
node for silence it has already been through.
`CONFIG_KFSW_GNDWDT_TIMEOUT_MIN_S` is a floor on what an operator can write,
which keeps a mistyped value from putting a node into a reset loop that takes a
pass to notice.

From the shell: `gndwdt show`, `gndwdt on`, `gndwdt off`,
`gndwdt timeout <seconds>` and `gndwdt contact`, which records contact without
a packet so a bench can hold the countdown open.

## Event record

`CONFIG_KFSW_EVENT` keeps events in a RAM ring. Each event has an ID, a
monotonic timestamp, a sequence number, a severity and a small payload. Logs
are for the console; events are for results you want to read later.

```text
log    "FTP put node=2 destination=/uplink/test.txt: PASS bytes=256 crc32=0ce9d363"
event  source=ftp id=1 payload={node:2, bytes:256, crc32:0x0ce9d363}
```

Events are small enough to downlink, a gap in the sequence shows lost records,
and rewording a log message doesn't break ground tools.

IDs and payload layouts are declared in each producer's public header. Boot
records the reset cause, the command service records every dispatch, and file
transfer records completed and failed transfers. Payloads are stored as given;
a producer that sends one over a link writes it big-endian.

The ring size is set in Kconfig. When it is full the oldest record is
overwritten and `events_overwritten` increases. Recording takes a short
spinlock, so it can be called from any context.

Read another node's events with `cmd <node> event_stats` and
`cmd <node> event_tail <age>`. The ring does not survive a reset.
