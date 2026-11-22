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
@BOOT sw=<image version> board=<board target> image=<application|golden> unit=<hardware id> reset=<flags> reset_rc=<result> reset_cause=<name>
@SOURCE <release commit or development>
@READY uptime_ms=<milliseconds>
```

The image version is `git describe` of the `k-fsw` repository when the build
is configured, and `version` prints the same string. `image` is `golden` in the
recovery image. `@SERVICES degraded failures=N` means N services failed to
start. `@READY` marks the end of startup. Release builds set the version and
source commit explicitly; see @ref development.

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
saved only when a parameter snapshot already exists, so after a new flash or
`param clear` every restart counts as the first until `param save` starts the
count.

### Image trials and reverts

Table 32 exposes read-only trial diagnostics. `version` prints them too.

| Offset | Parameter | Meaning |
| --- | --- | --- |
| `0x2c` | `boot_attempts` | Unconfirmed boots recorded for the running image; resets to zero on confirmation and saturates at `4294967294`. |
| `0x29` | `boot_revert_reason` | Last observed revert: 0 none, 1 unconfirmed image replaced with revert pending, 2 unconfirmed image replaced without an observed pending revert. |
| `0x2a` | `boot_trial_valid` | 1 when recovery and the current save succeeded; otherwise 0. |
| `0x3d` | `boot_trial_quarantined` | Rejected records moved to `/kfsw/boot-trial.bad` since startup; saturates at 255 and resets on reboot. |

The service identifies images by their signed SHA256 TLV and records a boot
immediately after storage mount, in `/kfsw/boot-trial.dat`, separate from the
parameter snapshot. A magic and CRC32 validate the record, and a synced
temporary file is renamed over the previous copy, so an interrupted write
leaves the previous copy. A boot that fails before the service records it is
not counted, and an interrupted save can lose the latest increment.

The revert reason is what the service observed: an image it last saw
unconfirmed is no longer running. Reason 1 adds that MCUboot had a revert
pending when that image was last recorded. The record keeps the previous image
identity. Confirming the running image resets the attempt count and keeps the
reason. A confirmation through `boot_confirmed` is saved at once; one through
the MCUboot shell is seen at the next startup.

A record that fails its checks (size, magic, field range or CRC) is moved from
`/kfsw/boot-trial.dat` to `/kfsw/boot-trial.bad`, replacing the previous
rejected record. The error is logged once and `boot_trial_quarantined`
increments. A fresh record starts at one for an unconfirmed image, or zero for
a confirmed image. An error opening or reading the file moves nothing: the
record may be sound, so it stays and diagnostics are invalid for that boot. If
the rename fails, the rejected record stays in place and the original load
error is returned.

Without MCUboot, including on native_sim, or when image identity, storage,
quarantine or saving fails, attempts reads `4294967295`, reason `255` and
validity 0. The quarantine counter remains readable even when diagnostics are
invalid. It is not persisted; the `.bad` file survives a reboot. A power cut
between quarantine and the fresh save leaves `.bad` as evidence, and the next
boot starts a fresh record.

`tests/boot-diagnostics-restart.sh <zephyr.exe>` runs the native_sim ztest
image twice against the same flash file. Power-cut and MCUboot rollback have
not been tested on hardware.

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

A report definition carries a retrieval class from 0 to 7, K-FSW's own
scheme, not Space Inventor's. Class 4 is the default, including for saved
definitions of versions 1 and 2. The class orders retrieval only; it changes
neither what is collected nor when.

Lower classes come down first. Assign reports by the decision the operator must
make, not by the device that produced the values:

| Class | Operational use |
| --- | --- |
| 0 | Survival: battery voltage and current, power-bus state. Decides whether to shed loads. |
| 1 | Fault diagnosis: reset cause, watchdog faults, safe mode, critical temperatures. Read before commanding recovery. |
| 2 | Whether recovery worked: power switches, subsystem health, command outcome. |
| 3 | Link and resource margins: radio errors, packet drops, free storage. |
| 4 | Routine platform telemetry; the default. |
| 5 | Payload status and recent science summaries. |
| 6 | Engineering trends: detailed temperatures, performance histories. |
| 7 | Bulk diagnostics and commissioning detail, while the configured history holds it. |

Define locally with `hk define 0 class=0 3:0`, or remotely with
`hk define 1 0 class=0 3:0` (node 1, report 0). `class=N` precedes the entries;
a value above 7 is refused, and omitting it gives class 4. The `hk_define`
command carries the same prefix in its entry-list argument. `hk show` prints
each definition's class, and read-only `hk_classes` (table 33, offset `0x30`)
holds one byte per report, 255 when undefined. With HK persistence, the
version-3 settings snapshot saves the class with each definition.

Retrieval takes an eight-bit mask in which bit n selects class n: `0x01` is
survival telemetry, `0x0f` classes 0 to 3, `0xff` all eight, zero nothing. The
ground bridge's `--class-mask 0x0f --count 12` asks for up to twelve RAM
samples from classes 0 to 3, lower classes first and newest collection first
within a class, across reports. Collection order holds even if UTC steps
backwards. Count and starting age apply to the selected stream, and a count
larger than the history returns fewer packets. File extraction keeps its
per-report filters and oldest-first order.

On CSP port 14, the existing five-byte request remains version, report, count,
starting age (big-endian u16). Class retrieval uses report 255 and adds a sixth
byte, the mask. Replies are one sample frame per packet, and the frame's
report number identifies its schema. The mask has no relation to CSP packet
priority.

Replacing a definition, including its class, discards its RAM history and
removes its store file. A collection already in progress is cancelled. A sample
recorded under the old definition is never read back using the new schema.

### Reading stored samples back

`hk store <report> <ms>` keeps samples in `/kfsw/hk/report<N>.bin`, a ring of
fixed-size slots where each record is the frame itself: version, report,
sequence, collection time, entry count, flags and the values. `hk stored
<report>` prints a stored record as `hk get` prints a live one.

Once the file is full the oldest slot is overwritten, so the readable window is
the last `CONFIG_KFSW_HK_STORE_CAPACITY` sequence numbers (256 by default).

A filter narrows what is read or extracted. Each part is optional and a zero
means no bound:

| Filter | Selects |
| --- | --- |
| `from=<seq>` | records at or after that sequence |
| `to=<seq>` | records at or before it |
| `since=<seconds>` | records collected at or after that time |
| `until=<seconds>` | records collected at or before it |
| `skip=<flags>` | leaves out records carrying any of those flags |

Sequence numbers wrap at 16 bits and are compared as a signed difference, so
a range across the wrap still works.

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

An extract counts the selection before writing and refuses with `-EAGAIN` if
the ring moved between the two passes.

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
or up to one second late. The target range is 1 to 2147483647, matching the
CSP clock. Set the clock before starting the procedure.

The initial wait and late tolerance must not exceed `CONFIG_KFSW_FBO_WAIT_MAX_S`
(default 600 seconds). UTC is checked every 100 ms. A forward step may make the
line due or too late; a backward step keeps waiting, with a monotonic deadline
of at most the configured maximum from entry. An unset/failed clock aborts the
line, including if it becomes unset while waiting. `fbo stop` wakes the wait
immediately; it cannot be bypassed by `on-error continue`. Other errors follow
the procedure's `on-error` policy. A pending wait is lost at restart.

### The time-tagged queue

The queue holds single commands that the node releases at a set time, with
nobody listening.

```text
fbo sched add +600 0 "noop"
fbo sched add @1900000000 2 "reboot 1234"
fbo sched list
fbo sched cancel 0
fbo sched clear
fbo sched status
```

`+seconds` counts from the moment the entry is accepted, on the monotonic clock,
so it is unaffected by anything that happens to the wall clock afterwards.
`@utc` is a UTC second and is refused with `-ENODATA` while the clock has not
been set this power cycle. The node after the time is where the command runs;
`0` is this one.

Adding, cancelling and clearing are ordinary commands, `fbo_sched_add`,
`fbo_sched_cancel` and `fbo_sched_clear`, so the ground reaches them over CSP the
same way the shell does.

An entry is addressed by the CRC32 of its content, so an uplink that arrives
twice does not queue the command twice: the second add reports `repeated=yes`
and names the slot already holding it. `fbo_sched_hash` is the CRC32 over the
whole queue, which is how the ground confirms the node holds the plan it sent.

An entry released later than `fbo_sched_latency_s` after its deadline reports
`overdue` and does not run. A release waits behind any command in flight, so
the service refuses a value below the command reply timeout.
`CONFIG_KFSW_FBO_SCHEDULE_BURST` (default 1) bounds how many entries one pass
releases, so a forward clock step does not release everything at once.

The queue is a fixed array of `CONFIG_KFSW_FBO_SCHEDULE_ENTRIES` slots (8 by
default) and does not survive a reset. A full queue refuses with `-ENOSPC`.
Cancelling an entry that has already run returns `-EALREADY`.

Table 34 carries the queue state: `fbo_sched_entries`, `fbo_sched_scheduled`, `fbo_sched_overdue`,
`fbo_sched_clock_set`, `fbo_sched_next_due_s`, `fbo_sched_next_due_utc`,
`fbo_sched_releases`, `fbo_sched_refusals`, `fbo_sched_overdues` and
`fbo_sched_hash`.

## Persistent event journal

`CONFIG_KFSW_JOURNAL` adds a fixed-size journal to the storage volume. The
Linux and Pico W images enable it. It accepts events from every source, boot
through limits. Boot and reset reports are always kept; other events must meet
`CONFIG_KFSW_JOURNAL_MIN_SEVERITY` (1, warning, by default). The text log and
the RAM event ring are separate.

```text
journal stats
journal tail 0
journal time 0
journal tail 2 0
```

Age 0 is the newest committed record. `journal tail` returns boot identity,
source, event ID, severity and the original payload in hex. `journal time`
returns the journal sequence, the event uptime in microseconds, UTC and its
validity. Uptime is taken with the event; the writer samples UTC. New records
shift the ages, so read `journal time` before and after the tail and check the
sequence is unchanged. The C read API returns all fields of one record. Reads
consume nothing.

The writer wakes every `CONFIG_KFSW_JOURNAL_FLUSH_MS` (1000 ms by default)
and drains at most `CONFIG_KFSW_JOURNAL_QUEUE_DEPTH` (16) records per batch,
writing and syncing each on its own thread. The emitter makes no filesystem or
wall-clock calls. A full queue drops incoming events and increments `drop`.
Boot-ready and retained last-words reports are queued once per boot; a failed
insertion is retried later. A repeated start allocates no new boot identity and
no second writer.

`/kfsw/journal.bin` holds `CONFIG_KFSW_JOURNAL_CAPACITY` records (128 by
default) in at most 9232 bytes: a 16-byte header and 128 checksummed 72-byte
slots, plus filesystem metadata. The queue and one pending record are in RAM
only.

At restart the journal validates the header and each slot, skips records with
bad checksums or incomplete trailing data, and resumes after the largest valid
sequence. It never reformats a corrupt or incompatible header. Capacity is in
the header, so changing it needs a new journal. Boot identities continue from
the retained history and restart at one on a new volume.

A failed write keeps the pending record and retries its slot after a rescan, so
a failed sync does not create two entries; later events can fill the queue and
be dropped meanwhile. `journal_stats` reports queue depth, drops, write errors,
damaged slots found at the last recovery, readiness and the last error. Records
queued or in progress are lost at power failure. Native fault tests cover these
cases; power-cut testing on hardware is pending.

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
sessions. It is off by default and in every target; `config/profiles/log-shell.conf`
enables it through `KFSW_EXTRA_CONF_FILE`. `param set log_shell 0` stops the
mirror and `param set log_shell 1` resumes it. The parameter is a u8 at table
25 offset `0x0d`, live, not persistent, and starts at 1. Messages before the
shell is ready and messages from interrupt context reach only the console.

`CONFIG_KFSW_LOG_HISTORY` keeps the most recent K-FSW messages in a RAM ring
(`CONFIG_KFSW_LOG_HISTORY_DEPTH`, 32 records by default, 1 to 128). Each
record keeps the message as a cbprintf package: the address of its format
string, the arguments, and any strings that were in RAM. The text is rebuilt
when read, so a record costs 128 bytes. A message too
long to package is kept as text, cut at 127 bytes and marked truncated.
`log history` prints up to 32 records locally, with sequence, uptime in
milliseconds, module, severity and up to 191 text bytes. Log filters apply
before retention. Zephyr logs, driver output and shell responses are not
captured.

### Debugger text mirror

`config/profiles/log-semihost.conf` copies each K-FSW console line, as
rendered with severity, colour escapes and newline, to a semihosting sink in
the platform layer. The UART console is unchanged. Markers are included, Zephyr
logs, driver output and shell replies are not. The profile does not use
`CONFIG_LOG_BACKEND_SEMIHOST` and does not change remote dictionary export.

From the workspace root, build the reference NUCLEO-L496ZG image:

```bash
./k-fsw/tools/build.sh nucleo_l496zg --semihost
./k-fsw/tools/debug.sh nucleo_l496zg
```

`--semihost` appends the profile to any `KFSW_EXTRA_CONF_FILE` list. It
accepts `nucleo_l496zg`, `nucleo_f746zg` and `frdm_k64f` and rejects other
targets before building.

To capture the mirror on a NUCLEO:

1. Start the debugger through the ST-Link and the OpenOCD runner.
2. Before the image runs, enter in GDB:

   ```text
   monitor reset halt
   monitor arm semihosting enable
   monitor arm semihosting_fileio disable
   continue
   ```

3. Read the lines on OpenOCD's own output, and keep the debugger connected
   for the whole session.

The sink calls Zephyr's `semihost_write` with stdout handle 1. Zero unwritten
bytes is success; a partial write or an error is dropped without retry or
recursive logging, and a concurrent mirror attempt is skipped.

The guard reads CMSIS DHCSR C_DEBUGEN on mainline Cortex-M and skips the call
when no debugger is attached, in a handler, with interrupts masked, or in
unprivileged thread mode. Cortex-M0/M0+, TrustZone and non-Cortex-M targets
return unsupported. The guard sees an attached probe, not whether its
semihosting handler is enabled, which is why step 2 comes before the image
runs. A detach can race the guard.

The overhead has not been measured. Each line halts the core for a host write,
and host latency has no bound. Do not enable the profile in flight builds; the
normal build compiles out the sink and the mirror. Native tests cover the sink
policy, text and failed or partial writes through linker wrappers; register
behaviour and detach timing need hardware and have not been tested.

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
(3000 ms) is logged as a warning.

The serving node's `log_remote_format` parameter picks how messages travel.
Text remains the compiled default. To start in dictionary mode, add this to an
extra configuration alongside `config/profiles/log-remote.conf`:

```text
CONFIG_KFSW_LOG_REMOTE_DEFAULT_DICTIONARY=y
```

`CONFIG_KFSW_LOG_REMOTE_DEFAULT_TEXT=y` selects text. The choice sets the
initial value and the reset default; a restored persistent value takes
precedence, and a write to `log_remote_format` applies at once.

The formats are:

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
and the decoder exits 1. The CRC compares rendered text, so a different image
that renders the same text also passes.

An old node sends packages without a CRC; a new decoder refuses to decode them.
Text mode remains usable. A new node's dictionary reply has a four-byte trailer
that an old requesting node rejects as malformed. A new requesting node prints
`pkg=00000000:<hex>`, and the `00000000:` guard makes an old decoder fail on a
four-byte package. Dictionary mode needs a new requester and decoder; text mode
works in both directions.

The Linux composition enables the service; other CSP compositions can use
`config/profiles/log-remote.conf`. Start the server after the CSP router.

`CONFIG_KFSW_LOG_HISTORY_RETAINED`, on by default, keeps the ring outside
`.bss` so a reset that preserves RAM leaves the messages that explain it
readable, and sequence numbers continue rather than restarting. The ring
carries a magic, a version, the depth, the record size and a CRC32, all
checked on first use, together with an identity of the running image, since
a package points into it. A ring that does not belong to the running image
starts clean, and a record whose slot disagrees reads as missing. A power
cycle clears the ring. Set the option to `n` for a ring that always starts
empty.

## Parameters

Parameters are named, typed values grouped in tables. Each service or module
defines its own table, with the storage, validators and change callbacks. The
application passes the enabled tables to `kfsw_param_init()`, which checks
them and builds a sorted index. Persistence and the CSP adapter use the same
index.

Validators check values without changing hardware or owner state. A scalar
`apply` callback performs work that can fail before storage is committed; its
errno reaches the local caller. `changed` runs after a successful write and
cannot reject it. Registry initialization copies defaults without calling
`apply`, so it does not drive GPIO before the module initializes.

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
layer that defines the table, which `param whichtables` shows in its `layer`
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

A definition set carries a `description`, one line saying what the table
holds, which `param whichtables` prints in its `holds` column. Give a new table
one.

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
| 37 | service | `table` | `kfsw-services/src/table-parameters/` |
| 38 | service | `remexec` | `kfsw-services/src/remexec-parameters/` |
| 39 | service | `scrub` | `kfsw-services/src/scrub-parameters/` |
| 40 | service | `limits` | `kfsw-services/src/limits-parameters/` |
| 50 | module | `radio_uhf` | `kfsw-modules/radio-uhf/parameters/` |
| 51 | module | `temp_example` | `kfsw-modules/temperature-sensor-example/parameters/` |
| 52 | module | `gpredict` | `kfsw-modules/gpredict/parameters/` |
| 67 | module | `hw_test` | `kfsw-modules/button-test/parameters/` |

A table is registered only when its service or module is enabled. The `fwu`
table is read-only. Core tables are in the application because the platform
and comms layers cannot depend on the parameter service.

### Write modes

The `mode` column of `param listall` comes from each definition:

| Mode | Meaning |
| --- | --- |
| `r` | Read-only |
| `rp` | Read-only, saved in the snapshot |
| `w` | Applied now, lost at reset |
| `wp` | Applied now and saved |
| `wb` | Read at the next boot, lost at reset |
| `wpb` | Saved and read at the next boot |

A definition with a change callback, or flagged live, applies at once. Writing
a `b` parameter prints that a reboot is needed. The service tables below use
the same letters.

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

`echo_enabled` in table 28 is off by default, so a scripted session does not
show each command twice. Turn it on to see what the console received, for
example when checking tab completion. The prompt colour is set at run time
because a Kconfig string cannot hold an escape sequence.

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
kfsw:~$ param listall 2
```

A named read asks the node for that one descriptor and then the value. A node
that doesn't answer the lookup is read by downloading its whole descriptor
list. The list is downloaded one indexed descriptor at a time and checked with
a table CRC, so a failed download leaves no partial cache.
`CONFIG_KFSW_PARAM_LIST_TIMEOUT_MS` (10000 ms) limits the download of one
table's descriptors, and so how long the shell waits for a node that does not
answer. `config/profiles/nucleo-testbench.conf` raises it to 40000 ms, because
a listing takes about 35 s over that bench's 57600 baud radio.

The cache holds `CONFIG_KFSW_PARAM_REMOTE_POOL_SIZE` descriptors. Value requests
are handled by a worker thread; when its queue is full the request is dropped
and `param_requests_dropped` increases.

Remote reads retry only `-ETIMEDOUT`, with at most three total read attempts
inside the caller's original deadline, including mutex waits. The default
budget is `CONFIG_KFSW_PARAM_LIST_TIMEOUT_MS + CONFIG_KFSW_PARAM_TIMEOUT_MS`
(1000 ms). A retry may request earlier batches again, so the values are not one
snapshot. Other errors return at once. Remote writes are single-shot.

On the requesting node, read-only `param_read_resends` (table 26, offset `0x20`,
wire ID `0x1a20`) counts extra request packets sent during read retries,
including descriptor requests. It starts at zero each boot, saturates at
`4294967295` and survives service restarts. It does not tell request loss from
reply loss, congestion or server drops. FWU's `resent` count is separate.

Each node validates the complete remote write before applying it. A request
containing a fallible scalar must contain exactly one value, including repeated
writes to the same parameter; batches of other values are accepted. A rejected
write keeps the previous value, and a failed application gets no success
acknowledgement. Cached peer values never run local appliers.

### Defaults, save and load

At boot every value starts from its compiled default, then the snapshot is
restored. `param set` changes RAM and runs the change callback. With
`param_autosave` on (the default), an accepted change to a persistent value
also writes the snapshot. Snapshot loads and persistent default resets refuse
multi-value operations containing a fallible scalar before changing any value.
Singleton operations return the applier's error if application fails.

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

### What may be persistent

A snapshot is restored as a whole, so a restored value that calls back into its
owner can fail in the middle of the restore. Registration therefore refuses a
parameter that is persistent and has an `.apply` callback, with `-EINVAL`
naming the parameter. One such entry would otherwise make every other
persistent value in the snapshot unrestorable.

`KFSW_PARAM_FLAG_OWNER_STORE` is the exemption. It marks a parameter whose
owner keeps the value somewhere else, and the restore skips it, so the owner
may be both persistent and fallible. `boot_attempts_left` and
`boot_attempts_max` in table 32 are the canonical pair: persistent, with an
`.apply` that can fail, and held in the platform boot store rather than the
snapshot.

Read-only and persistent needs no exemption: `boot_count` is saved in the
snapshot and refused to a writer.

`kfsw_param_init()` applies the rule at registration, so a composition that
breaks it fails on the bench, not at restore.

## Parameter tables as files

A snapshot is written by the node, covers every persistent value and is
adopted at boot. A table file is written on the ground, addresses one table and
is adopted on command. Both use the same value encoding.

```text
  ground            node
  edit a file  ->   ftp put   ->  table check <path>   nothing applied yet
                                  table load  <path>   all of it, or none
                                  table revert         the replaced values back
```

The file has a 20-byte header (magic `KTBL`, version 1, payload size, entry
count, the table identifier and a CRC32) and then one entry per value: the
offset within the table, the type code, the length and the big-endian value.
`CONFIG_KFSW_TABLE_MAX_ENTRIES` (16 by default) caps the entries in a file and
sizes the read buffer and the values held for a revert.

A load is refused whole. Before anything is written the service checks the
header and the CRC, then every entry: the offset has to name a parameter in that
table, the type code has to be the one that parameter is written as, the length
has to match it, the parameter must not be read-only, the same offset must not
appear twice, and the parameter's own range check has to pass. The first entry
that fails stops the load and is reported by position, offset and errno.
Nothing is applied, so the table stays as it was.

A successful load keeps the values it replaced, and `table revert` puts them
back. Only the most recent load can be undone, and only once. The held values
do not survive a reset, so follow a load that must outlive one with
`param save`.

A file containing a fallible scalar must contain only that value, and the same
holds for a revert. A failed load returns the application errno and keeps the
previous revert state; a failed revert can be retried. A revert does not undo
any physical effect.

`table dump <table> <path>` writes the running values as a file, leaving out
read-only parameters and types no file carries. To change a table:

1. Run `table dump <table> <path>` on the node and fetch the file with
   `ftp get`.
2. Edit the value on the ground and upload the file with `ftp put`.
3. Run `table check <path>`, then `table load <path>`.
4. Check the result with `table show`; undo with `table revert` if needed.

Table 37 reports the state, the last file, and the load, rejection and revert
counters.

## Storage

K-FSW mounts a [LittleFS](https://github.com/littlefs-project/littlefs)
volume in flash at `/kfsw` and, where the composition has a RAM disk, a
scratch volume at `/kfsw/tmp` that `kfsw_storage_tmp_mount()` formats at every
boot: 32 KiB on Linux and the NUCLEO, 16 KiB on the Pico W. The platform layer handles init, mount, unmount and capacity. Once it is mounted, services use the normal Zephyr
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
`build/linux/kfsw-storage.bin` when started with the runner. The FRDM-K64F profile keeps
storage disabled. Pico W uses its own persistent and scratch layout; see
@ref targets.

### Mount and format

The first mount uses `FS_MOUNT_FLAG_NO_FORMAT`. If LittleFS reports the
partition as corrupt (`EFAULT`), the whole partition is scanned. A fully erased
partition is formatted and mounted; anything else is reported as an error and
left as it is. Services that need storage report their own errors when it is
not mounted.

### One filesystem, two block layers

LittleFS is the only filesystem K-FSW mounts, on every target. What lies
under it changes with the flash. LittleFS keeps directory metadata in a pair of erase blocks, so a partition
needs more than two of them to hold a directory tree at all: measured on the
hosted image, two and three 256 KiB blocks both fail on the first directory
with `-28`, the F746's four 32 KiB sectors fail on the second, and 4 KiB blocks
work. The STM32F746 has 256 KiB sectors, so LittleFS cannot sit on its internal
flash directly.

The ZMS disk below presents Zephyr's ZMS subsystem as a `disk_access` device,
with small logical sectors over the large physical ones. The mount path, the
platform storage API and the services are the same on both boards. The L496,
whose 2 KiB pages suit LittleFS, stays on flash directly.

### Optional ZMS block translation

`config/profiles/f746-zms-storage.conf` and
`config/profiles/f746-zms-storage.overlay` opt into the platform ZMS disk.
Use them with the shared `f746-recovery-flash.overlay` map, after
`f746-minimal.conf`. No target enables this backend by default.
The selected partition must contain two erase-aligned physical sectors;
the F746 map reserves `[0x80000, 0x100000)`, two 256 KiB sectors.

The disk exposes 48 sectors of 4096 bytes (192 KiB before LittleFS overhead).
Logical sector N uses ZMS ID N+1. ZMS owns append, recovery and compaction;
the adapter serializes access and checks complete records with data CRC.
The mandatory `tools/zephyr-patch.sh` correction propagates close-ATE write
errors before ZMS advances to another physical sector.
Writes are synchronous. A multi-sector request validates its complete range
before writing, but it is not atomic: earlier sectors can persist when a
later sector fails. After a write error, unmount and reinitialize before
using the disk again.

Before ZMS mount, the adapter scans the partition and recognizes the pinned
Zephyr 4.4.0 version 1, 32-bit-ID empty-sector headers. Blank media may be
initialized. Unknown nonblank media, unsupported valid headers, and media
with all headers destroyed are refused without writes. This guard is tied
to the pinned on-media format; check it when updating Zephyr. LittleFS may
format only when every logical sector is erased.

The profile also enables persistent parameters and FTP in the A image. Keep it
out of the golden image, which uses the recovery uploader and never mounts the
file region. Check each build's fit with the linker memory report and the
`zephyr.signed.bin` size against the 256 KiB A partition. No firmware update or
recovery writer uses this disk.

The native suite is `tests/unit/platform_storage_zms`. Erase stalls, wear,
watchdog margins, UART and CAN behaviour and power-loss behaviour have not been
measured on the F746.


## File transfer

K-FSW FTP is the project's own protocol and is not compatible with Internet
FTP, FTPS, SFTP or TFTP. It supports list, stat, mkdir, rm, put and get. The server
listens on CSP port 9 and requires RDP and CRC32. Each message has a 24-byte
header with the request ID, offset, total size and file CRC.

Remote FTP read-only refusals (`/hk` and `/boot`) use wire status 13 and
return `-EROFS`, like local refusals. Existing status numbers are unchanged;
older clients map the new status to `-EIO`. The FTP client returns this errno
to shell callers. Failed put/get transfers also record positive errno 30 in
transfer failure events and increment failure counters. Mkdir and rm have no transfer event.

`ftp rm [node] <path>` removes one file or empty directory and frees its storage.
Directories with entries are refused without deleting their contents. The storage
root is never removable. A missing path returns `NOT_FOUND` (`-ENOENT`). A non-empty
directory returns `-ENOTEMPTY` locally and the existing `IO_ERROR` (`-EIO`) remotely.

Paths are virtual and rooted at `/kfsw/ftp` on each node:

```text
FTP path              Zephyr path
/build/sample.bin     /kfsw/ftp/build/sample.bin
/exchange/result.dat  /kfsw/ftp/exchange/result.dat
```

Paths are up to 96 bytes. Relative paths, empty components, `.` and `..`,
backslashes, control characters and embedded NULs are rejected. The sandbox
confines paths; it does not control access. Three more roots sit beside it,
and a listing of `/` shows each one that exists as a directory:

| Path | Volume | Writable |
| --- | --- | --- |
| `/hk` | housekeeping sample files, in flash | no |
| `/boot` | firmware slots, with `CONFIG_KFSW_FWU_FILES` | no |
| `/tmp` | `/kfsw/tmp`, RAM, with `CONFIG_KFSW_STORAGE_TMP` | yes |

`/tmp` is LittleFS on a RAM disk, formatted at every boot; nothing in it
survives a reset. The composition picks the disk with the `kfsw,tmp-disk`
chosen node: 32 KiB in the Linux composition and in SRAM2 on the NUCLEO,
16 KiB on the Pico W.

A transfer that would leave less than `CONFIG_KFSW_FTP_SPACE_MARGIN_BYTES`
(4096 by default) free on its destination volume is refused with `-ENOSPC`
before the partial file is created. The firmware upload path writes the slot
and is not checked.

### The local node

`ls`, `stat`, `mkdir` and `rm` addressed to the node's own CSP address run
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

A download is the same in the other direction:

```text
client                              server
GET (path, held bytes, resume)   ->
                                 <- GET_INFO (size, CRC32, offset it starts at)
                                 <- DATA (that offset)
writes <path>.part               <- DATA (next offset)
                                 <- GET_RESULT
```

The receiving side always writes `<path>.part`, syncs it, checks it and renames
it. An existing file is left alone until the rename.

### Resuming a transfer

A transfer interrupted by a lost link continues when the same file is asked
for again, in either direction.

Whichever node receives keeps `<path>.part` after an interruption instead of
deleting it, and records what it is going to become in `<path>.part.map`: a
magic, a version, the intended size and CRC32, and a CRC32 over those fields.
That is the server for an upload and the client for a download, next to the
destination file either way.

On an upload the sender already knows the size and CRC32, so they travel in the
PUT request and the server continues only when its note matches them. On a
download the receiver is the one holding a partial and the only one that knows
which file it belongs to, so the GET request carries the note's size and CRC32
together with how much is already on disk, and the sending node compares them
against the file it holds now. Any mismatch gets a zero offset in `GET_INFO`,
and the client discards its partial and starts again.

The resume point is the partial's own size, not a number from the note, because a
reset can leave the note ahead of what reached flash. The CRC32 of those bytes is
recomputed before the transfer continues, and the final check still covers the
whole file.

Chunks arrive strictly in order, so the note holds no chunk map. `PUT_READY`
and `GET_INFO` carry the offset the transfer starts from, and a request without
the resume flag discards the partial and starts at zero.

A partial for a file that is never asked for again stays on disk until that
path is transferred or deleted.

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

No shell command runs a registry command by name. Each is reached from its
shell group: `status`, `event`, `journal`, `hk`, `fbo sched`, `golden`,
`reboot`, `csp reboot` and `gndwdt`. A new command that operators run needs a
place in a group.

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
a command again after a lost reply.

Table 28 counts dispatch outcomes. The four counters start at zero each boot,
are not saved, and wrap modulo 2^32; they do not saturate. Only recorded
dispatch outcomes count: a call refused before dispatch, or one made before
the registry is initialised, is not counted. An outcome is classified by its
final status, whether or not a handler ran.

| Parameter | Description in the table |
| --- | --- |
| `cmd_invoked` | Recorded dispatch outcomes since boot; wraps at 2^32 |
| `cmd_failed` | Outcomes other than OK, UNKNOWN, DENIED; wraps at 2^32 |
| `cmd_unknown` | UNKNOWN outcomes since boot; wraps at 2^32 |
| `cmd_rejected` | DENIED outcomes since boot; wraps at 2^32 |
| `cmd_registered` | Commands in the frozen registry |
| `cmd_timeout_ms` | Timeout for new remote invocations |

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

The node keeps `CONFIG_KFSW_COMMAND_RETRY_SLOTS` tickets (8) for
`CONFIG_KFSW_COMMAND_RETRY_WINDOW_MS` (60000) by default. A new call gets BUSY
when all are in use, and an expired or unknown ticket gets UNAVAILABLE. A
restart discards every ticket.

#### Wire format

Version 2 adds an eight-byte token to the header (20 bytes total), preserving
argument encoding. Opcodes 3/4 prepare and return a ticket; opcode 5 executes
it, and opcode 2 returns the result. Prepare carries a random client nonce;
execute carries the returned random server ticket. The cache matches source
node, ticket, command ID, request ID, argument count and the entire payload.
A repeated prepare within its lifetime returns the same ticket without
extending its deadline. A repeated execute returns the recorded result. A
failed entropy read prevents allocation. Handlers run synchronously and need
their own execution bounds. Ticket lifetimes do not follow clock changes.

## Remote shell execution

`CONFIG_KFSW_REMEXEC` lets an operator read which shell commands a node offers
and run one of them, on CSP port 8, with its output coming back to the node
that asked. The Linux, NUCLEO L496 and Pico W images enable it. Capturing
output needs a second shell instance, one more thread of
`CONFIG_SHELL_STACK_SIZE`, measured on the L496 at 6924 bytes of flash and 8832
of RAM.

A command is unreachable until the composition marks it in the list handed to
`kfsw_remexec_init()`. The listing reads the same array the executor checks.
There is no wildcard, no debug mode and no PIN; a composition that marks
nothing lists nothing and refuses every execution.

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
arguments too, so mark only commands whose every argument is safe to run from
the ground. The reference list holds read-only commands that finish promptly
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
malformed one is refused with its own reason. One server thread serves one
connection at a time, with a listen backlog of one, so a second request waits
for the first to finish and times out at the client if it waits too long.

### The output cap and what truncation looks like

A reply carries at most `CONFIG_KFSW_REMEXEC_OUTPUT_MAX` bytes, 200 by
default, which keeps one reply inside a single CSP buffer on the narrowest
link. The service counts every byte the command printed, keeps the first cap
bytes and reports the rest as dropped:

```text
node: 2
command: version
output: 200 of 264 bytes, truncated, 64 dropped
K-FSW: v1.1.0
...
```

The status and the truncation are reported separately: a command that returns
zero and overruns the cap reports `status: ok` with `truncated`. The listing shares the cap: a long allowlist comes back
truncated, and `remexec <node> get <command>` narrows it.

### Execution and timeout

Execution runs on the remote execution server thread, so a slow handler cannot
stall the CSP router. A shell handler runs to completion and cannot be
aborted, so `CONFIG_KFSW_REMEXEC_TIMEOUT_MS` (5000) is the budget the serving
node measures an execution against and reports as `timeout` afterwards, and
the client's wait for a reply. A handler that never returns holds the server
thread: no later request is served, and each client times out. Only the
allowlist guards against that.

If the requesting node goes away mid-execution, the command still runs to
completion. The reply is sent on the connection and discarded by the link,
nothing is queued and nothing is retried, and the serving node's counters
still record the execution. A client that timed out does not know whether the
command ran; read table 38 or the log on the next pass.

A request addressed to the local node is refused: the capture shell cannot be
driven from the shell that asked.

Table 38, all read-only:

| Parameter | Meaning |
| --- | --- |
| `remexec_accepted` | Requests whose command was allowed and run |
| `remexec_refused` | Requests refused before any command ran |
| `remexec_truncated` | Replies that could not carry all the output |
| `remexec_offered` | Commands marked for remote execution |
| `remexec_refusal` | Why the most recent request was refused |
| `remexec_running` | Whether a command is running on this node now |
| `remexec_last_result` | What the last command returned; zero before any ran |
| `remexec_last_ms` | How long the last command took |
| `remexec_last_command` | The last command line asked for, with its arguments |

See @ref kfsw_services_remexec.

## Resource monitor

The resource monitor reads the kernel's thread list every
`CONFIG_KFSW_RESMON_PERIOD_MS` (10000 ms by default) and records stack use.
Threads need no registration. Check both the percentage used and the bytes
left, under the workload the node will run.

Table 36 exposes the measurements:

| Parameter | Access | Meaning |
| --- | --- | --- |
| `stack_worst_used` | r | Highest stack use on any thread since start |
| `stack_last_used` | r | Highest stack use in the most recent sweep |
| `stack_alert_used` | w | A thread at or above this raises an event; `CONFIG_KFSW_RESMON_ALERT_PERCENT`, 90 |
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

On native_sim the threads run on host stacks, so the values say nothing about
stack headroom.

`KFSW_RESMON` selects `INIT_STACKS`, `THREAD_STACK_INFO`, `THREAD_MONITOR` and
`THREAD_NAME`. Leave the service out where the target cannot afford them.

## Image scrub

The update service checks an image as it arrives and the bootloader checks the
slot it boots. Nothing else reads the slot that is not running, or the recovery
image, ever again, so a bit flip in either is found when the image is needed
and not before. `KFSW_SCRUB` re-reads them and compares a CRC32 against a
reference that outlives a reset.

| Region | Where | Image starts at |
| --- | --- | --- |
| `slot0` | `slot0_partition` | the partition start |
| `slot1` | `slot1_partition` | `kfsw_fwu_slot_write_offset()`, one sector in |
| `golden` | the `kfsw,golden-partition` node | the partition start |

The storage partition is not a region: it changes with every ordinary write.

A region is swept only when it holds an image, meaning the bootloader's magic
at that region's image offset. The length swept comes from the image header, so
an erased tail does not change the result and an empty secondary slot reads as
`empty` rather than as a fault.

| Parameter | Access | Meaning |
| --- | --- | --- |
| `<region>_reference` | wpb | The learned CRC32; 0 means none is |
| `<region>_measured` | r | What the last sweep read |
| `<region>_bytes` | r | Image bytes swept |
| `<region>_state` | r | 0 empty, 1 unknown, 2 ok, 3 mismatch, 4 unreadable |
| `scrub_sweeps` | r | Sweeps completed |
| `scrub_mismatches` | r | Regions found changed, once per crossing |
| `scrub_last_ms` | r | Monotonic ms at the end of the last sweep |
| `scrub_regions` | r | Regions this build can sweep |
| `scrub_running` | r | Whether the periodic sweep is running |

The reference is learned on command, never silently: a region whose reference
is unknown reports `unknown` and never `ok`. `scrub learn <region>` sweeps the
region and takes what is on flash now as the reference, then writes the
parameter snapshot, because a reference held only in RAM would be relearned at
every boot and could never report a mismatch.

A mismatch logs an error and raises an event once per crossing. It re-arms when
a sweep finds the region matching again, after a new image is written and
learned.

From the shell: `scrub status`, `scrub sweep [region]` and
`scrub learn <region>`.

`KFSW_SCRUB` depends on `KFSW_FWU`, because the secondary image sits where the
update service writes it. A sweep reads hundreds of kilobytes of flash, so
`KFSW_SCRUB_PERIOD_MS` defaults to 3600000 ms; the read is chunked and yields
between chunks, so it does not starve the watchdog feed. The recovery
composition leaves scrub off: it has no parameter persistence, so it could not
hold a reference.

## Limits

`KFSW_LIMITS` watches local or remote numeric parameters against a bound and releases a
command when one is crossed. The rules are one text file,
`limits/limits.txt` under the FTP root, uploaded as a procedure is and read at
boot and on `limits load [file]`:

```text
# parameter          cmp  bound  dwell  node  action
stack_worst_used     >    90     3      0     event_stats
```

The first five whitespace separated fields are the parameter, the comparison
(`>`, `>=`, `<`, `<=`, `==`, `!=`), the bound, the dwell and the node. Everything
after them is the action, a registered command with its arguments, kept as
written. A line starting with `#` is a comment; there are no trailing comments.

A parameter may have a `<node>:` prefix, for example `12:t_x > 5 1 0 noop`.
The prefix selects where to read; the node column still selects where to release
the action. `0:` and this node's own address read locally. The prefix must be an
unsigned unicast CSP address; an empty prefix or name, a second colon and a
broadcast address are refused. A nonzero prefix needs `KFSW_PARAM_CSP`.
Remote rules load without contacting the peer and bind their type on a successful
read. A float-only bound with `==` or `!=` is refused at load.

The bound is read as the parameter's own type, so a fractional bound on an
integer parameter is refused, and `==` and `!=` are refused on a float. The
dwell is how many consecutive sweeps must satisfy the comparison, from 1 to 255.
The action must be a command from the command registry, such as `event_stats`
or `noop`, with the right arguments; shell commands are not actions. It is
checked when the file loads, not when it fires.

A file with any bad line is refused whole. The previous rules stay in force,
`limits_parse_errors` rises, and `limits status` and `limits_error_line` name
the line and what was wrong with it.

A rule fires once per crossing. The action is released through the scheduler of
file based operations, one second out, so the release is recorded and may
address another node. The rule then latches, and re-arms on the first sweep that
finds the comparison false. A release that fails is counted in `limits_failures`
and tried again on the next sweep.

Rules are swept every `CONFIG_KFSW_LIMITS_INTERVAL_MS` (10000 ms by default).
Remote reads run on a dedicated workqueue, `CONFIG_KFSW_LIMITS_STACK_SIZE`
(2560 bytes) at `CONFIG_KFSW_LIMITS_PRIORITY` (4), below the router and
servers. Each sweep gives remote I/O half the
interval, capped at 5000 ms; manual sweeps with interval zero use 5000 ms.
Rules are grouped by node, up to 16 names per call. Each group gets its own
deadline from the time left divided by the remaining groups, with a 10 ms
minimum. Name resolution can cost an exchange per cache miss, and the descriptor
cache holds one node, so calls do not imply one wire exchange per node.
When less than 10 ms remains, the sweep logs a warning and leaves unfinished
rules for the next sweep, resuming at the first unread rule. Deferred rules keep
their streak and latch and are excluded from the shell's evaluated count.

A failed remote read resets each affected streak, keeps the latch and releases
nothing. Both failure counters rise once per affected rule. `limits_failed_by`
names the last node whose read failed and clears when that node next answers or
a file loads successfully. A returned non-numeric type, float equality or a bound
that does not fit resets the streak and adds only to `limits_failures`; it never
compares. Local rules run first, so a rule watching `limits_failed_by` sees a
remote failure on the next sweep.

| Parameter | Mode | Meaning |
| --- | --- | --- |
| `limits_rules` | r | Rules in force |
| `limits_running` | r | Whether the periodic sweep is scheduled |
| `limits_enabled` | wp | 0 stops the sweeps and keeps the loaded rules |
| `limits_interval_ms` | wp | Between sweeps; 0 for none, otherwise 100 to 3600000 |
| `limits_sweeps` | r | Sweeps completed while enabled |
| `limits_fires` | r | Actions released |
| `limits_failures` | r | Parameter reads and releases that failed |
| `limits_remote_failures` | r | Remote reads that failed, counted per rule |
| `limits_failed_by` | r | Last node whose read failed; 0 on recovery or load |
| `limits_parse_errors` | r | Files refused for a bad line |
| `limits_last_fired_ms` | r | Uptime of the last release; 0 before any |
| `limits_last_fired` | r | Index of the rule that fired last; 255 before any |
| `limits_error_line` | r | Line that got the last file refused |

From the shell: `limits status`, `limits list`, `limits load [file]` and
`limits sweep`. `KFSW_LIMITS` depends on `KFSW_PARAM` and `KFSW_FBO`.

## Watchdogs

Three watchdogs answer three different questions. Each resets the node when
its answer is no. @ref fault_recovery shows how they fit with the other fault
mechanisms.

| Watchdog | Proves | Fed by | Timeout | Enabled in |
| --- | --- | --- | --- | --- |
| Hardware | the CPU still runs | health, while every watched component reports | 8000 ms on the NUCLEO | `CONFIG_KFSW_WATCHDOG`, NUCLEO and Pico W |
| Health | the threads still run | the main loop (`app`) and the CSP router probe (`csp`) | 4000 ms and 8000 ms deadlines | `CONFIG_KFSW_HEALTH`, NUCLEO and Pico W |
| Ground | somebody still talks to the node | a ground watchdog feed over CSP | 24 h, from 2 h to 5 days | `CONFIG_KFSW_GNDWDT`, Linux, NUCLEO and Pico W |

### Hardware watchdog

The MCU's independent watchdog, IWDG on the STM32L4, runs from its own
low-speed oscillator and cannot be stopped once started. Zephyr drives it
through its watchdog driver. `main()` arms it first, before storage and the
services, so a hang anywhere in startup still ends in a reset; a keep-alive on
the system workqueue feeds it until health takes over at the end of startup.
native_sim has no hardware watchdog. The [NUCLEO target](../targets/index.md#watchdog) explains
why it is the independent watchdog and not `watchdog0`.

### Health

Health decides when the hardware watchdog is fed. Each watched component has a
deadline; while all of them report in time, health feeds the watchdog, and when
one misses its deadline health stops feeding and the hardware watchdog resets
the board. A board stuck with interrupts running but its threads blocked is
reset this way. Health needs the hardware watchdog, so it is off on Linux.
Table 31 publishes the service and one slot per named component. See
@ref health-guide for registration, the slots and reacting before the reset.

### Ground watchdog

The ground watchdog resets a node nobody has talked to for too long, so a
spacecraft that lost its way out of contact eventually starts again from a
known state. The countdown restarts only on command 16, `ground_wtd`, carrying
the exact text `KFSWWSFK`, received over CSP. Any node may send it. Ping,
telemetry, parameter traffic and local feed attempts do not count. The timeout
is 2 hours to 5 days (7200-432000 seconds), with a 24-hour default (86400
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
guards against an accidental feed and authenticates nobody. A repeated legacy
request feeds again.
With a ticketed request, a duplicate returns the saved result without feeding
again.

A ground station built with `tools/k-ground` carries the service so it can feed
others, but its own countdown starts disarmed: nobody feeds the ground. `gndwdt
on` arms it.

Table 35 exposes:

| Parameter | Mode | Meaning |
| --- | --- | --- |
| `gndwdt_enabled` | w | Whether the countdown is armed |
| `ground_wtd_timeout` | w | Timeout in seconds, 7200 to 432000; default 86400 |
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

`CONFIG_KFSW_EVENT` keeps events in a RAM ring of `CONFIG_KFSW_EVENT_RING_DEPTH`
records (32 by default). Each event has a source, an ID, a monotonic timestamp,
a sequence number, a severity and a small payload, so it can be decoded without
parsing console text.

```text
console  FTP put 2 /build/test.txt -> /uplink/test.txt: PASS
         bytes: 256
         crc32: 0ce9d363
event    source=ftp id=1 payload={node:2, bytes:256, crc32:0x0ce9d363}
```

Sequence gaps identify missing events.

There are ten sources, numbered 1 to 10, and `kfsw_event_emit()` refuses any
other:

| Source | Name | Records |
| --- | --- | --- |
| 1 | `boot` | The reset cause |
| 2 | `command` | Every recorded dispatch, with ID, node and status |
| 3 | `ftp` | Completed and failed transfers |
| 4 | `app` | Health faults and module events |
| 5 | `fbo` | File based operations |
| 6 | `gndwdt` | Ground watchdog expiry |
| 7 | `resmon` | Stack alerts |
| 8 | `table` | Table file loads, rejections and reverts |
| 9 | `scrub` | Image mismatches |
| 10 | `limits` | Rules that fired |

IDs and payload layouts are declared in each producer's public header. Payloads
are stored as given; a producer that sends one over a link writes it
big-endian. When the ring is full the oldest record is overwritten and
`events_overwritten` increases. Recording takes a short
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
| `gpredict` | 52 | Pass tracking driven by Gpredict on the ground: rotator bearing and corrected frequency |
| `button-test` | 67 | A board button and three LEDs, for bench tests |

The [kfsw-modules README](https://github.com/dgonzalez97/kfsw-modules#readme)
says how a module is laid out and how to add one.
