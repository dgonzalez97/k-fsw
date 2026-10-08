# Services {#services}

[TOC]

## Services

`kfsw-services` provides boot markers, logging, parameters, persistence,
uploadable tables, files, commands, events, health, housekeeping, file based
operations, and firmware updates. The application selects and starts them. The
[kfsw-services README](https://github.com/dgonzalez97/kfsw-services#readme)
lists each one with the Kconfig option that turns it on.

Storage is in `kfsw-platform`. Network services use the router and interfaces
in `kfsw-comms`. For housekeeping and Yamcs, see @ref ground. For uploads and
boot recovery, see @ref firmware_update. The API is in @ref kfsw_services.

## Boot and readiness markers

At startup the boot service reads and clears the reset cause and logs these
markers (see [Logging](#logging) for how markers differ from other messages):

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

### Build revisions

`boot_image` identifies the `k-fsw` revision. `boot_revisions` also lists the
platform, services, communications, modules, libcsp and libparam revisions
used in the build, eight hex digits each; `status` prints them one a line:

```text
kfsw:~$ version
K-FSW: v1.0.4-77-g3516c1a
Revisions: app:3516c1a1 plat:049b7289 svc:ed838173 comms:b4289702 mod:ad39ef41 csp:d62491f5 param:c8a7c104
```

Read `boot_revisions` from table 32. A trailing `+` marks a repository with
uncommitted changes at build time.

### Restart count

`boot_count` in table 32 counts restarts across the life of the node. It is
saved only when a parameter snapshot already exists: a new flash, or one
cleared with `param clear`, keeps nothing stored, which is the state to recover
from a bad saved value. Until something saves a snapshot, every restart counts
as the first; `param save` starts the count.

### Image trials and reverts

Table 32 exposes read-only trial diagnostics. `version` prints them too.

| Offset | Parameter | Meaning |
| --- | --- | --- |
| `0x2c` | `boot_attempts` | Unconfirmed boots recorded for the running image; resets to zero on confirmation and saturates at `4294967294`. |
| `0x29` | `boot_revert_reason` | Last observed revert: 0 none, 1 unconfirmed image replaced with revert pending, 2 unconfirmed image replaced without an observed pending revert. |
| `0x2a` | `boot_trial_valid` | 1 when recovery and the current save succeeded; otherwise 0. |

The service identifies images by their signed SHA256 TLV and records a boot
immediately after storage mount. It saves `/kfsw/boot-trial.dat` in the existing
persistent filesystem, independently of parameter snapshots and autosave. A
magic and CRC32 validate the record; a synced temporary file is renamed over
the previous copy. An interrupted write leaves the previous good copy intact.
The count survives power cuts after a completed save. Boots that fail before
the service records them cannot be counted, and an interrupted save may lose
the latest increment.

The reason is the one the service can establish, not a verdict from the
bootloader: an image previously observed unconfirmed is no longer running.
Reason 1 additionally records that MCUboot scheduled a revert while that image
was last recorded; the service had not recorded its confirmation. The previous image
identity is kept in the same record. It does not establish why confirmation was missed or distinguish a bootloader
revert from another replacement of an unconfirmed image. The reason remains
stored when the running image is confirmed; only the attempt count resets.
Confirming through `boot_confirmed` saves that reset immediately. Confirmation
through the separate MCUboot shell is reflected at the next service startup.

Without MCUboot, including on native_sim, or if image identity, storage or
snapshot validation fails, attempts is `4294967295`, reason is `255`, and
validity is 0. A corrupt record is preserved and reported invalid rather than
silently restarting the count at one.

Software recovery coverage runs the native_sim ztest image twice against the
same flash file with `tests/boot-diagnostics-restart.sh <zephyr.exe>`. Physical
power-cut and MCUboot rollback acceptance require hardware and have not been
run for this change.

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

### Retrieval classes

K-FSW uses its own eight-class scheme, not Space Inventor's representation.
A report definition carries a three-bit class value, 0..7, alongside the schema
it already holds. Class **4** is the default for existing definitions, older
saved definitions (versions 1 and 2), and compositions that set no class.
The class only orders retrieval: it changes neither what is collected nor when.

Lower numbers come down first. Assign reports by the decision the operator must
make, rather than by the device that produced the values:

| Class | Operational use |
| --- | --- |
| 0 | Survival now: battery voltage, battery current and power-bus state. With eight minutes of pass left, retrieve this first to decide whether loads must be shed. |
| 1 | Immediate fault diagnosis: reset cause, watchdog faults, safe-mode state and critical temperatures. Read this before commanding recovery. |
| 2 | Whether the recovery worked: power-switch states, subsystem health and command outcome. Get this during the same pass. |
| 3 | Link and resource margins: radio errors, packet drops and free storage. Use this to plan the rest of the pass. |
| 4 | Routine platform telemetry. The default keeps an unchanged composition's newest-first behaviour. |
| 5 | Payload status and recent science summaries. Retrieve after the vehicle and link are understood. |
| 6 | Engineering trends: detailed temperatures and long performance histories. Defer to a later pass when bandwidth is short. |
| 7 | Bulk diagnostics and commissioning detail that can wait a week, provided the configured history retains it. This is not a retention guarantee. |

Define locally with `hk define 0 class=0 3:0`, or remotely with
`hk define 1 0 class=0 3:0` (node 1, report 0). `class=N` must precede the
entries; values above 7 are refused. Omit it to use class 4. The remote command
`hk_define` carries the same prefix in its entry-list text argument.
`hk show` prints each definition's class. Parameter table 33 (`hk`), offset
`0x30`, publishes read-only `hk_classes`: one byte per report, 255 when
undefined. Read it locally or through remote PARAM; use `hk define` to change
the definition and its class together. With HK persistence composed, the
version-3 settings snapshot saves the class on each accepted definition change.

Retrieval uses a separate eight-bit selection mask: bit n selects class n.
`0x01` requests survival telemetry; `0x0f` selects classes 0..3; `0xff`
selects all eight; zero selects nothing. For example, the ground bridge's
`--class-mask 0x0f --count 12` asks for up to twelve selected RAM samples.
It returns lower classes first and newest collection first within each class,
including across reports. A requested count larger than available history can
produce fewer packets (the bridge reports its timeout). Collection order is
used even if UTC steps backwards. Count and starting index apply to the
selected, ordered stream. This path reads RAM history; file extraction keeps
its existing per-report filters and oldest-first order.

On CSP port 14, the existing five-byte request remains version, report, count,
starting age (big-endian u16). Class retrieval uses report 255 and adds a sixth
byte, the selection mask. Replies remain one unchanged sample frame per packet;
the frame's report number identifies its schema. The mask is a retrieval
selection, not the report's class value or a CSP packet priority.

Replacing a definition, including its class, discards its RAM history and
removes its store file. A collection already in progress is cancelled. A sample
recorded under the old definition is never read back using the new schema.

### Reading stored samples back

`hk store <report> <ms>` keeps samples in `/kfsw/hk/report<N>.bin`, a ring of
fixed-size slots where each record is the frame itself: version, report,
sequence, collection time, entry count, flags and the values. So a stored record
needs no decoding to be served again, and `hk stored <report>` prints it exactly
as `hk get` prints a live one.

The record count is what the ring holds, not what was ever collected. Once the
file is full the oldest slot is overwritten, and the readable window is the last
`CONFIG_KFSW_HK_STORE_CAPACITY` sequence numbers.

A filter narrows what is read or extracted. Each part is optional and a zero
means no bound:

| Filter | Selects |
| --- | --- |
| `from=<seq>` | records at or after that sequence |
| `to=<seq>` | records at or before it |
| `since=<seconds>` | records collected at or after that time |
| `until=<seconds>` | records collected at or before it |
| `skip=<flags>` | leaves out records carrying any of those flags |

Sequence numbers wrap at 16 bits, so a window is compared as a signed
difference and a range across the wrap still selects what it should.

`hk extract <report> <path> [filters]` writes the selected records to a file for
downlink. It has a 20-byte header (magic `KHKD`, version, report, record size,
record count, the first and last sequence, and a CRC32 over the whole file with
those four bytes zeroed), then the records unchanged, so the ground decodes them
with the report definition it already has. The file is written beside the target
and renamed over it, so an interrupted extract leaves the previous one.

Written under `/kfsw/hk` it is downloadable straight away, because file transfer
already serves that directory read-only:

```bash
hk extract 0 /kfsw/hk/pass.bin since=1790744000
ftp get 1 /hk/pass.bin ./pass.bin
```

An extract counts the selection before writing and refuses with `-EAGAIN` if the
ring moved between the two passes, rather than leaving a file whose header
disagrees with its records.

## File based operations

`fbo run <name>` runs the commands in a procedure file, `fbo stop` ends the
run, and `fbo status` shows what it did. The file is
`/procedures/<name>` under the FTP root, `/kfsw/ftp/procedures` on the node,
so it is uploaded like any file:

```text
ftp put 2 /procedures/check-in.txt /procedures/check-in.txt
fbo run check-in.txt
```

File size, line length and scanned bytes are limited, and comments count toward
the scan limit. An I/O error stops the procedure. Examples to copy are in
[tests/procedures/examples](https://github.com/dgonzalez97/k-fsw/tree/main/tests/procedures/examples);
the FBO smoke test runs each of them.

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

`wait-until <unix-seconds> <late-tolerance-seconds>` waits on the existing UTC
clock. For example, `wait-until 1900000002 1` permits a step at that UTC second
or up to one second late. Set the clock before starting the procedure; it does
not synchronize time.
The target range is 1..2147483647, matching the current CSP clock.

The initial wait and late tolerance must not exceed `CONFIG_KFSW_FBO_WAIT_MAX_S`
(default 600 seconds). UTC is checked every 100 ms. A forward step may make the
line due or too late; a backward step keeps waiting, with a monotonic deadline
of at most the configured maximum from entry. An unset/failed clock aborts the
line, including if it becomes unset while waiting. `fbo stop` wakes the wait
immediately; it cannot be bypassed by `on-error continue`. Other errors follow
the procedure's `on-error` policy. Execution time is not guaranteed, and
pending waits are lost at restart.

## Persistent event journal

`CONFIG_KFSW_JOURNAL` adds a fixed-size journal to the existing storage volume.
The Linux image enables it. Boot/reset reports are always selected; other
events must meet `CONFIG_KFSW_JOURNAL_MIN_SEVERITY` (warning by default).
Text logging and the existing RAM event ring remain separate.

```text
journal stats
journal tail 0
journal time 0
journal tail 2 0
```

Age 0 is the newest committed record. `journal tail` returns boot identity,
source, event ID, severity and the original payload in hex. `journal time`
returns the journal sequence, original event uptime in microseconds, UTC and
its validity. Uptime is captured with the event; UTC is sampled by the writer.
New records shift the ages. Read `journal time` before and after the tail and
check that the sequence is unchanged.
The C read API returns all fields from one record. Reads do not consume data.

The writer wakes every `CONFIG_KFSW_JOURNAL_FLUSH_MS` (1000 ms by default),
draining at most the configured queue depth (16) per batch. Each record is
written and synced on the worker. No filesystem or wall-clock driver calls
run in the emitter/ISR. A full queue drops incoming events and increments
`drop`. Boot-ready and retained-lastwords reports are queued once per boot;
a failed queue insertion permits a later attempt. Repeated start calls do
not allocate another boot identity or start another writer.

The default file `/kfsw/journal.bin` retains 128 records and occupies at most
9232 bytes on the volume: a 16-byte header plus 128 checksummed 72-byte slots.
Filesystem metadata, copy-on-write space and flash wear are additional. The
queue and one pending record are RAM-only. Adjust severity and flush interval
for the expected event rate. Bulk text belongs in the console log.

Restart recovery validates the header and each slot, skips records with bad
checksums and incomplete trailing data, and resumes after the largest valid
sequence. It never reformats a corrupt or incompatible header. Capacity is
part of the header: changing it requires an explicit migration/new journal.
Boot identities advance from retained history and restart at one on a new
volume; they are unique only within that history.

Write failures retain one pending record and retry its slot after rescanning,
so a failed sync does not create two journal entries. Subsequent events can
fill the queue and be dropped. `journal_stats` exposes queue depth, drops,
write errors, damaged slots found in the latest recovery, readiness and the
last error. Records still queued, failed or in progress can be lost at power
failure. Native fault tests cover these policies; physical power-cut
qualification remains pending.

## Logging {#logging}

Code reports what happened through the log, not with `printk`. A file names
its module once and calls the macro for the level:

```c
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_RADIO
#include <kfsw/services/log.h>

kfsw_log_info("Radio session to node %u established", peer);
kfsw_log_error("Radio protection not started: %d", result);
```

Shell output answers the operator who typed a command; a log records what the
node did, whether anyone asked or not. Shell handlers print with
`shell_print`, and everything else logs. A message reaches the console, the
history described below and, over CSP, the ground.

Two kinds of line go through the log with rules of their own:

- **Markers.** `@SERVICES`, `@BOOT`, `@SOURCE` and `@READY` are written with
  `kfsw_log_marker()`. No level hides them, and they are printed without a
  level tag so the line still starts with the marker that scripts look for.
  The history keeps them like any message.
- **The CSP packet trace.** With `csp debug on`, libcsp reports every packet;
  each line is logged at INFO under the `csp` module, without libcsp's
  colours. Leave it off on a busy link: it fills the history quickly.

A node that starts logs, among others:

```text
[INFO] CSP initialized as node 1
[INFO] CSP router started
[INFO] Ground watchdog started, timeout 86400 s, armed
@SERVICES ok failures=0
@READY uptime_ms=0
```

Messages have four levels: DEBUG, INFO, WARNING and ERROR.
`CONFIG_KFSW_LOG_MIN_LEVEL` removes the lower levels from the build. At
runtime `log_level` sets a global level and `log_levels` one per module; a
message has to pass both. An invalid level is rejected.

Each message is printed as one console line, with newlines replaced by
spaces. Lines are coloured by level: errors red, warnings yellow, info white
and debug dim. The colour codes wrap the whole line, so `[LEVEL] message`
stays intact for scripts. `log_color` turns colour off.

`CONFIG_KFSW_LOG_SHELL=y` also sends messages and markers to active shell
sessions. The option defaults off; Linux and NUCLEO keep it disabled unless an
extra configuration enables it. `config/profiles/log-shell.conf` enables the
capability through `KFSW_EXTRA_CONF_FILE`. With PARAM built, `param set log_shell 0`
stops mirroring and `param set log_shell 1` resumes it immediately. This u8
parameter is table 25 offset `0x0d`, live and not persistent; it starts at 1
when the capability is built. Messages before the shell is ready and messages
from interrupt context still reach only the existing console backend.

`CONFIG_KFSW_LOG_HISTORY` keeps the most recent K-FSW messages in a RAM ring
(32 records by default, configurable from 1 to 128). Each record keeps the
message as a cbprintf package: the address of its format string, the
arguments, and any strings that were in RAM. Text is rebuilt when someone
reads it, so a record costs 128 bytes instead of its full text. A message too
long to package is kept as text, cut at 127 bytes and marked truncated.
`log history` prints up to 32 records locally, with sequence, uptime in
milliseconds, module, severity and up to 191 text bytes. Log filters apply
before retention. Zephyr logs, driver output and shell responses are not
captured.

### Remote log

`CONFIG_KFSW_LOG_REMOTE` serves the history, and the journal when
`CONFIG_KFSW_JOURNAL` is set, on CSP port 16 with CRC32. Another node reads
them from its shell:

```text
kfsw-ground# log remote 1 8          # newest 8 messages of node 1
kfsw-ground# log remote 1 32 2       # warnings and errors only
kfsw-ground# journal remote 1 10     # newest 10 journal records
```

Reads are bounded (1 to 32 records), do not remove anything, and do not need
UTC. A node that does not answer within `CONFIG_KFSW_LOG_REMOTE_TIMEOUT_MS`
(3 s) is logged as a warning.

The serving node's `log_remote_format` parameter picks how messages travel:

| Value | Format | On the requesting shell |
| --- | --- | --- |
| 0 | text, formatted on the serving node, at most 190 bytes | the message |
| 1 | dictionary: the package as held plus a four-byte IEEE CRC32 | `pkg=00000000:<hex> crc32=<hex>` |

Dictionary records are smaller on a radio link and carry no text. Decode a
capture on a host with the ELF of the exact image that sent them:

```bash
./k-fsw/tools/ground/log-decode.py --elf build/nucleo_l496zg/zephyr/zephyr.elf capture.txt
```

It reads the format strings from the ELF and the package with Zephyr's
dictionary parser, and prints each line with the text in place of the package
and CRC only when the CRC agrees with the node-rendered string. The CRC covers
at most 191 bytes, with CR/LF replaced by spaces and without the terminating NUL.
A missing, malformed or different CRC leaves the record marked `not decoded`
and returns exit 1. This checks rendered text agreement, not ELF identity:
another image rendering byte-identical text still passes, as can a CRC32
collision.

An old node sends packages without a CRC; a new decoder refuses to decode them.
Text mode remains usable. A new node's dictionary reply has a four-byte trailer
that an old requesting node rejects as malformed. A new requesting node prints
`pkg=00000000:<hex>`: the `00000000:` guard makes an old host decoder attempt
a four-byte package and fail explicitly. Upgrade the requester and decoder for
dictionary mode; text mode remains compatible in both directions.

The Linux composition enables the service; other CSP compositions can use
`config/profiles/log-remote.conf`. Start the server after the CSP router.

`CONFIG_KFSW_LOG_HISTORY_RETAINED`, on by default, keeps the ring outside
`.bss` so a reset that preserves RAM leaves the messages that explain it
readable, and sequence numbers continue rather than restarting. The ring
carries a magic, a version, the depth, the record size and a CRC32, all
checked on first use, together with an identity of the running image, since
a package points into it. A ring that does not belong to the running image
starts clean, and a single record whose slot disagrees reads as missing.
A power cycle clears RAM, so nothing is retained across one. Set the option
to `n` for a ring that always starts empty.

## Parameters

Parameters are named, typed values grouped in tables. Each service or module
defines its own table, with the storage, validators and change callbacks. The
application passes the enabled tables to `kfsw_param_init()`, which checks
them and builds a sorted index. Persistence and the CSP adapter use the same
index.

Local writes call `validate` before updating storage, then call `changed`.
A rejected local write keeps the previous value. Change callbacks run after
the write and cannot reject it.

```text
KFSW_PARAM              local tables and API
KFSW_PARAM_PERSISTENCE  needs KFSW_PARAM and KFSW_STORAGE
KFSW_PARAM_CSP          needs KFSW_PARAM and KFSW_CSP
KFSW_FTP                needs KFSW_STORAGE and KFSW_CSP
```

`KFSW_PARAM` does not need CSP; `tests/param-local-smoke.sh` builds a
local-only composition.

### Tables

A parameter is addressed by table and offset. Table numbers are split by the
layer that defines the table, which `param tables` shows in its `layer`
column:

| Numbers | Used by |
| --- | --- |
| 0 | Reserved, never valid |
| 1-24 | Application, platform and comms |
| 25-49 | Services, one table each |
| 50-99 | Modules |
| 100-255 | Reserved; rejected by the current registry |

On the wire the table is the high byte and the offset the low byte. Offsets
are unique inside a table. Names are unique on the node and up to
`KFSW_PARAM_NAME_MAX` (32) characters; a longer name is refused at
registration.

A definition set can carry a `description`, one short line saying what the
table holds; `param tables` prints it in its `holds` column. Every table in
the five repositories has one, and a new table should too.

| ID | Layer | Name | Source |
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
| 35 | service | `gndwdt` | `kfsw-services/src/gndwdt-parameters/` |
| 36 | service | `resmon` | `kfsw-services/src/resmon-parameters/` |
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

`sample` refreshes a value before a local or remote read. It runs under the
table lock and must not call the parameter API.

Remote writes use `kfsw_param_read_stored_entry()` to skip sampling, which
would otherwise overwrite the incoming value before `changed` applies it.

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
`CONFIG_KFSW_PARAM_LIST_TIMEOUT_MS` (3 s) limits the download, and so how long
the shell waits for a node that does not answer. The radio compositions raise
it to 40 s, since a listing takes about 35 s over a 57600 baud radio.

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

## Parameter tables as files

A snapshot is written by the node, covers every persistent value across all
tables, and is adopted at boot. A table file is the other direction: written on
the ground, addressing one table, and adopted when an operator says so. Both use
the same value encoding, so one decoder reads either.

```text
  ground            node
  edit a file  ->   ftp put   ->  table check <path>   nothing applied yet
                                  table load  <path>   all of it, or none
                                  table revert         the replaced values back
```

The file has a 20-byte header (magic `KTBL`, version 1, payload size, entry
count, the table identifier and a CRC32) and then one entry per value: the
offset within the table, the type code, the length and the big-endian value.
`CONFIG_KFSW_TABLE_MAX_ENTRIES` caps how many entries a file may carry, and sets
the read buffer and the number of replaced values held for a revert.

A load is refused whole. Before anything is written the service checks the
header and the CRC, then every entry: the offset has to name a parameter in that
table, the type code has to be the one that parameter is written as, the length
has to match it, the parameter must not be read-only, the same offset must not
appear twice, and the parameter's own range check has to pass. The first entry
that fails stops the load and is reported by position, offset and errno, so an
operator knows which line of the file to fix. Nothing is applied, so a file with
one bad value leaves the table exactly as it was.

A successful load keeps the values it replaced, and `table revert` puts them
back. Only the most recent load can be undone, and only once; there is no stack
of loads. The held values do not survive a reset, so a load that needs to outlive
one is followed by `param save`.

`table dump <table> <path>` writes what is running as a file, so the usual way
to change a table is to fetch that, edit one value on the ground and upload it
back rather than composing a file from the manual. Read-only parameters and
types no file carries are left out, because a load could not write them anyway.

Table 37 reports the state, the last file, and the load, rejection and revert
counters.

## Storage

K-FSW mounts a [LittleFS](https://github.com/littlefs-project/littlefs)
volume in flash at `/kfsw` and, where the composition has a RAM disk, a 32 KB
scratch volume at `/kfsw/tmp` that `kfsw_storage_tmp_mount()` formats at every
boot. The platform layer handles init, mount, unmount and capacity. Once it is mounted, services use the normal Zephyr
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

Remote FTP read-only refusals (`/hk` and `/boot`) use wire status 13 and
return `-EROFS`, like local refusals. Existing status numbers are unchanged;
older clients map the new status to `-EIO`. The FTP client returns this errno
to shell callers. Failed put/get transfers also record positive errno 30 in
transfer failure events and increment failure counters. Mkdir has no transfer event.

Paths are virtual and rooted at `/kfsw/ftp` on each node:

```text
FTP path              Zephyr path
/build/sample.bin     /kfsw/ftp/build/sample.bin
/exchange/result.dat  /kfsw/ftp/exchange/result.dat
```

Paths are up to 96 bytes. Relative paths, empty components, `.` and `..`,
backslashes, control characters and embedded NULs are rejected. The sandbox is
not access control. Three more roots sit beside it, and a listing of `/` shows
each one that exists as a directory:

| Path | Volume | Writable |
| --- | --- | --- |
| `/hk` | housekeeping sample files, in flash | no |
| `/boot` | firmware slots, with `CONFIG_KFSW_FWU_FILES` | no |
| `/tmp` | `/kfsw/tmp`, RAM, with `CONFIG_KFSW_STORAGE_TMP` | yes |

`/tmp` is LittleFS on a RAM disk, formatted at every boot; nothing in it
survives a reset. The composition picks the disk with the `kfsw,tmp-disk`
chosen node: 32 KB in the Linux composition and in SRAM2 on the NUCLEO.

A transfer that would leave less than `CONFIG_KFSW_FTP_SPACE_MARGIN_BYTES`
(4096 by default) free on its destination volume is refused with `-ENOSPC`
before the partial file is created. The firmware upload path writes the slot
and is not checked.

### The local node

`ls`, `stat` and `mkdir` addressed to the node's own CSP address run
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
PUT (path, size, CRC32, resume)  ->
                                 <- PUT_READY (offset to start from)
DATA (that offset)               ->
DATA (next offset)               -> writes <path>.part
                                 <- PUT_RESULT after sync, check and rename
```

A download is the same in the other direction: GET, GET_INFO, DATA and
GET_RESULT. The receiving side always writes `<path>.part`, syncs it, checks
it and renames it. An existing file is left alone until the rename.

### Resuming an upload

An upload interrupted by a lost link continues on the next attempt, without an
operator asking. Uploading the same file again is the whole interface.

The server keeps `<path>.part` after an interruption instead of deleting it, and
records what it is going to become in `<path>.part.map`: a magic, a version, the
intended size and CRC32, and a CRC32 over those fields. On the next upload of
the same path it reads that note, and continues only when the size and CRC32
match the new request. A note for a different version of the file is ignored and
the partial is overwritten, so an upload never wastes a pass discovering at the
end that it joined two different files.

The resume point is the partial's own size, not a number from the note, because a
reset can leave the note ahead of what reached flash. The CRC32 of those bytes is
recomputed before the transfer continues, and the final check still covers the
whole file, so a resumed upload is verified exactly like a fresh one.

Chunks arrive strictly in order, so the note holds no chunk map. `PUT_READY`
carries the offset to start from, and a client that does not set the resume flag
in its request gets the original behaviour: the partial is discarded and the
upload starts at zero.

A partial for a file that is never uploaded again stays on disk until that path
is uploaded or deleted.

The server has one acceptor and one worker and answers `busy` to a second
connection. Client calls share one static workspace behind a mutex. Each
receive waits up to `ftp_timeout_ms` (15 s by default). `ftp generate` is
limited to 32768 bytes; transfers are limited by the 32-bit size field, free
space and the link.

## Command service

`CONFIG_KFSW_COMMAND` enables the command registry used by the shell groups
and by remote callers on CSP port 11. A command has a name, a numeric ID, up to
four typed arguments and a result.

```text
shell    status 2       finds info by name, asks node 2
ground   ID 2, port 11  found by ID
             |
   same definition, validation and handler
```

There is no shell command that runs any command by name. Each one is reached
from the group an operator looks in, `status`, `event`, `journal`, `hk`,
`reboot` or `gndwdt`, so a new command that operators run also needs its place
in a group.

Commands are registered at build time as sets and the registry is fixed at
startup. Duplicate IDs or names, missing handlers and too many arguments are
rejected. Handlers run in the caller's thread under the command mutex. Remote
requests use the command server thread; local calls use the shell or calling
thread. Handlers do not run on the CSP router thread.

A version 1 message has a 12-byte big-endian header: version, opcode, status,
argument count, command ID, request ID and payload size. Type-length-value
arguments follow the header. Every length is checked before use, and a message
always fits one CSP packet.

There is no authentication. The request carries the source node and an
authentication flag that is always false. See @ref kfsw_services_command.

A handler runs to completion and returns a status and an optional short text.
Legacy requests are not deduplicated, so check the node state before sending
a command again after a lost reply. Remote parameters use the parameter service,
not commands.

### Ticketed retries

`CONFIG_KFSW_COMMAND_RETRY` lets a remote request end in `--retry`, for example
`csp reboot 2 0000 --retry`.
It is enabled in the Linux image. Both peers must support it and have working
entropy. An invocation first reserves a ticket, then executes it;
up to three attempts per phase reuse the same request bytes. An unsupported
peer causes the call to fail. There is no fallback to the legacy protocol.

Typing the command again starts a new operation. If all result attempts fail,
or a handler resets the node, the outcome can remain unknown; inspect the
node before starting another operation. A request without `--retry` keeps its
one-shot behaviour.

The node keeps eight tickets for 60 seconds by default. New calls get BUSY when
all are in use, and an expired or unknown ticket gets UNAVAILABLE. A restart
discards every ticket. Tickets stop duplicates within their lifetime; they are
not authentication and do not survive a reset.

#### Wire format

Version 2 adds an eight-byte token to the header (20 bytes total), preserving
argument encoding. Opcodes 3/4 prepare and return a ticket; opcode 5 executes
it, and opcode 2 returns the result. Prepare carries a random client nonce;
execute carries the returned random server ticket. The cache matches source
node, ticket, command ID, request ID, argument count and the entire payload.
A repeated prepare within its lifetime returns the same ticket without
extending its deadline. A repeated execute returns the recorded result. A
failed entropy read prevents allocation. Handlers still run synchronously and
need their own execution bounds. Changing clocks does not affect ticket
lifetimes.

## Remote shell execution

`CONFIG_KFSW_REMEXEC` lets an operator read which shell commands a node offers
and run one of them, on CSP port 8, with its output coming back to the node
that asked. The Linux image enables it. It is not composed into the NUCLEO
image yet: capturing output costs a shell instance, and that footprint has not
been measured on the board.

**The allowlist is the security boundary.** A command is unreachable until the
composition marks it in the list handed to `kfsw_remexec_init()`, and the
listing reports exactly the marked commands. Discovery and permission are one
list, so they cannot disagree. There is no wildcard, no debug mode and no PIN.
A composition that marks nothing answers the listing with an empty reply and
refuses every execution, which is what a node that never opted in should do.

### How a composition marks a command

The list lives beside the composition, in
`k-fsw/app/src/remexec/remexec_allowlist.c`, not in the service:

```c
static const struct kfsw_remexec_entry remexec_entries[] = {
	{.command = "storage info", .help = "Filesystem totals and mount state"},
};
```

`command` is the shell command as typed, with its subcommand and without
arguments. `help` is the line the listing shows. Marking a command marks its
arguments too, so mark only commands whose whole argument space is safe to run
from the ground: the reference list is read-only commands that finish promptly
and print little.

`kfsw_remexec_init()` refuses a list rather than serving part of one. It
rejects more entries than `CONFIG_KFSW_REMEXEC_MAX_ENTRIES` (16), an entry
without a command or help text, a command longer than 64 bytes, a command
holding a byte that is not printable, and a repeated command. A rejected list
leaves the node offering nothing.

### What an operator sees

A refusal names the command and says it is not offered, never a generic
failure:

```text
remexec: 'storage test' is not offered for remote execution
```

A command line is at most 64 bytes and must be printable ASCII; a longer or
malformed one is refused with its own reason. Only one execution runs at a
time: a second request is refused BUSY rather than queued.

### The output cap and what truncation looks like

A reply carries at most `CONFIG_KFSW_REMEXEC_OUTPUT_MAX` bytes, 200 by
default, which keeps one reply inside a single CSP buffer on the narrowest
link. The service counts every byte the command printed, keeps the first cap
bytes and reports the rest as dropped, so the count is exact:

```text
node: 2
command: version
output: 200 of 264 bytes, truncated, 64 dropped
K-FSW: v1.1.0
...
```

Whether the command succeeded is separate from whether its output fitted. A
command that returns zero and overruns the cap reports `status: ok` with
`truncated`; a command that fails and prints two bytes reports the failure and
no truncation. The listing shares the cap: a long allowlist comes back
truncated, and `remexec <node> get <command>` narrows it.

### Limits

Execution runs on the remote execution server thread, so a slow handler cannot
stall the CSP router. A shell handler runs to completion and cannot be
aborted, so `CONFIG_KFSW_REMEXEC_TIMEOUT_MS` (5000) is the budget the serving
node measures an execution against and reports as `timeout` afterwards, and
the client's own wait for a reply. A handler that never returns holds the
executor and every later request is refused BUSY; the allowlist is the control
for that, not the timeout.

If the requesting node goes away mid-execution, the command still runs to
completion. The reply is sent on the connection and discarded by the link,
nothing is queued and nothing is retried, and the serving node's counters
still record the execution. A client that timed out therefore does not know
whether the command ran; read table 38 or the log on the next pass.

Capturing output costs a second shell instance, so composing the service adds
one thread of `CONFIG_SHELL_STACK_SIZE`. A node cannot be asked to run its own
commands through `remexec`: the capture shell cannot be driven from the shell
that asked, so a request addressed to the local node is refused.

Table 38 publishes what the service counted, so a refusal is visible without
reading a log:

| Parameter | Access | Meaning |
| --- | --- | --- |
| `remexec_accepted` | r | Requests whose command was allowed and run |
| `remexec_refused` | r | Requests refused before any command ran |
| `remexec_truncated` | r | Replies that could not carry all the output |
| `remexec_offered` | r | Commands marked for remote execution |
| `remexec_refusal` | r | Why the most recent request was refused |

See @ref kfsw_services_remexec.

## Resource monitor

The resource monitor periodically reads the kernel's thread list and records
stack use. Threads need no registration. Check both the percentage used and
the bytes left, under the workload the node will run.

Table 36 exposes the measurements:

| Parameter | Access | Meaning |
| --- | --- | --- |
| `stack_worst_used` | r | Highest stack use on any thread since start |
| `stack_last_used` | r | Highest stack use in the most recent sweep |
| `stack_alert_used` | rw | A thread at or above this raises an event |
| `stack_worst_free` | r | Unused bytes on the busiest thread |
| `stack_worst_size` | r | Stack size of that thread |
| `stack_sweeps` | r | Sweeps completed |
| `stack_alerts` | r | Times a sweep first found a thread at the alert level |
| `stack_threads` | r | Threads the last sweep could read |
| `stack_running` | r | Whether the periodic sweep is running |
| `stack_worst_thread` | r | Thread holding the highest stack use |

An event is raised when stack use reaches the alert threshold. Another alert
requires a sweep below the threshold first.

From the shell: `resmon show`, `resmon sample` to sweep now, and
`resmon alert <percent>`.

MCU targets measure the configured thread stacks. Native simulation uses host
stacks, so its reported values do not measure stack headroom.

`KFSW_RESMON` selects `INIT_STACKS`, `THREAD_STACK_INFO`, `THREAD_MONITOR` and
`THREAD_NAME`. Measurements use the initial stack fill and the kernel thread
list. Disable the service if the target cannot afford that overhead.

## Watchdogs

Three watchdogs answer three different questions. Each resets the node when
its answer is no.

| Watchdog | Proves | Fed by | Timeout | Enabled in |
| --- | --- | --- | --- | --- |
| Hardware | the CPU still runs | health, while every watched component reports | 8000 ms on the NUCLEO | `CONFIG_KFSW_WATCHDOG`, NUCLEO |
| Health | the threads still run | the main loop (`app`) and the CSP router probe (`csp`) | 4000 ms and 8000 ms deadlines | `CONFIG_KFSW_HEALTH`, NUCLEO |
| Ground | somebody still talks to the node | a ground watchdog feed over CSP | 24 h, from 2 h to 5 days | `CONFIG_KFSW_GNDWDT`, Linux and NUCLEO |

### Hardware watchdog

The MCU's independent watchdog, IWDG on the STM32L4, runs from its own
low-speed oscillator and cannot be stopped once started. Zephyr drives it
through its watchdog driver; `kfsw-platform` arms it after the services have
started, so a slow boot is not reset before the shell is up. native_sim has no
hardware watchdog. The [NUCLEO target](../targets/index.md#watchdog) explains
why it is the independent watchdog and not `watchdog0`.

### Health

Health decides when the hardware watchdog is fed. Each watched component has a
deadline; while all of them report in time, health feeds the watchdog, and when
one misses its deadline health stops feeding and the hardware watchdog resets
the board. A board stuck with interrupts running but its threads blocked is
reset this way. Health needs the hardware watchdog, so it is off on Linux.

### Ground watchdog

The ground watchdog resets a node nobody has talked to for too long, so a
spacecraft that lost its way out of contact eventually starts again from a
known state. The countdown restarts only on command 16, `ground_wtd`, carrying
the exact text `KFSWWSFK`, received over CSP. Any node may send it. Ping,
telemetry, parameter traffic and local feed attempts do not count. The timeout
is 2 hours to 5 days (7200–432000 seconds), with a 24-hour default (86400
seconds).

From another node's console, feed node 2 and read it back:

```text
kfsw-ground# gndwdt feed 2
node: 2
fed: yes
ground_wtd_cnt: 86400
ground_wtd_timeout: 86400

kfsw-ground# gndwdt show 2
node: 2
ground_wtd_cnt: 86390
ground_wtd_timeout: 86400
```

A node does not feed itself: `gndwdt feed` refuses its own address. Underneath
it is command 16, `ground_wtd`, with `KFSWWSFK` or `get`, which is what a ground
tool sends. Both nodes need `CONFIG_KFSW_GNDWDT` and
`CONFIG_KFSW_COMMAND_CSP`. The command uses port 11 by default. The magic word
checks intent; it is not authentication. A repeated legacy request feeds again.
With a ticketed request, a duplicate returns the saved result without feeding
again.

A ground station built with `tools/k-ground` carries the service so it can feed
others, but its own countdown starts disarmed: nobody feeds the ground. `gndwdt
on` arms it.

Table 35 exposes:

| Parameter | Access | Meaning |
| --- | --- | --- |
| `gndwdt_enabled` | rw | Whether the countdown is armed |
| `ground_wtd_timeout` | rw | Timeout in seconds, 7200–432000; default 86400 |
| `ground_wtd_cnt` | r | Seconds remaining; zero when stopped, expired or waiting to reset |
| `gndwdt_since_s` | r | Seconds since startup or the last valid feed |
| `gndwdt_contacts` | r | Valid feed commands accepted |
| `gndwdt_expiries` | r | Times the timeout passed |
| `gndwdt_last_node` | r | Sender of the last valid feed |
| `gndwdt_running` | r | Whether the service was started |

Set the timeout with `param set ground_wtd_timeout 86400` locally or
`param set 2 ground_wtd_timeout 86400` remotely. The timeout keeps wire ID
`35:0x04`; its former name was `gndwdt_timeout_s`. The countdown uses `35:0x18`.

`gndwdt show`, `gndwdt on`, `gndwdt off` and `gndwdt timeout <seconds>` act on
the local countdown. Changing the timeout or re-enabling the watchdog does not
restart it. A node re-enabled after its deadline can reset at the next check.
Send a valid feed first. A reset already queued is not cancelled.

## Event record

`CONFIG_KFSW_EVENT` keeps events in a RAM ring. Each event has an ID, a
monotonic timestamp, a sequence number, a severity and a small payload. Event
IDs and payloads can be decoded without parsing console text.

```text
console  FTP put 2 /build/test.txt -> /uplink/test.txt: PASS
         bytes: 256
         crc32: 0ce9d363
event    source=ftp id=1 payload={node:2, bytes:256, crc32:0x0ce9d363}
```

Sequence gaps identify missing events.

IDs and payload layouts are declared in each producer's public header. Boot
records the reset cause, the command service records every dispatch, and file
transfer records completed and failed transfers. Payloads are stored as given;
a producer that sends one over a link writes it big-endian.

The ring size is set in Kconfig. When it is full the oldest record is
overwritten and `events_overwritten` increases. Recording takes a short
spinlock, so it can be called from any context.

Read another node's events with `event stats <node>` and
`event tail <node> <age>`. The ring does not survive a reset.

## Modules

`kfsw-modules` holds the code for a specific device or subsystem, built on
these services. Each module has its own parameter table, numbered 50 to 99.

| Module | Table | What it is |
| --- | --- | --- |
| `radio-uhf` | 50 | Holybro SiK UHF radio: identity, status and optional link encryption |
| `temperature-sensor-example` | 51 | The MCU's die temperature, sampled on a work queue; the module to copy |
| `boton-test` | 67 | A board button and three LEDs, for bench tests |

The [kfsw-modules README](https://github.com/dgonzalez97/kfsw-modules#readme)
says how a module is laid out and how to add one.
