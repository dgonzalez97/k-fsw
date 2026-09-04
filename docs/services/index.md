# Services and Storage {#services}

## Service ownership

`kfsw-services` contains reusable application behavior. It depends on
`kfsw-platform` for capabilities such as mounted storage and, only when a
CSP-backed service is enabled, on `kfsw-comms`. The executable application
selects services and calls their lifecycle APIs; it does not duplicate their
algorithms.

The current service set is intentionally compact:

- boot/readiness markers;
- runtime-filtered logging;
- a bounded local parameter index assembled from component-owned definitions;
- optional parameter snapshots;
- an optional CSP parameter adapter using the libparam wire format; and
- a K-FSW file-transfer client/server over CSP/RDP.

Storage lifecycle and monotonic time live in `kfsw-platform`, but are covered
here because parameter persistence and FTP depend directly on storage.

## Boot and readiness markers

The boot service reads and clears Zephyr's hardware reset-cause flags through
the platform API, prints a structured `@BOOT` line, logs the elapsed startup
time, and prints `@READY`.

```text
@BOOT sw=kfsw-dev board=<CONFIG_BOARD_TARGET> reset=<flags> reset_rc=<result>
@READY uptime_ms=<monotonic milliseconds>
```

These markers are stable automation points used by software and HIL tests.
They are not a health report. The current startup path can log a service error
and still reach `@READY`; a test that needs storage or CSP must also check the
corresponding service state.

## Logging

The logging API has four message severities: DEBUG, INFO, WARNING, and ERROR.
`CONFIG_KFSW_LOG_MIN_LEVEL` removes calls below a compile-time threshold. A
runtime atomic threshold can further suppress compiled messages without
rebuilding.

```text
source log call
      |
compiled at CONFIG_KFSW_LOG_MIN_LEVEL?
      | yes
runtime severity >= current log_level?
      | yes
print one bounded line to the Zephyr console
```

Messages are formatted into a fixed 192-byte buffer. Embedded newlines and
carriage returns are replaced with spaces so each event occupies one console
line. This is a basic console logging service, not yet a structured event
store, telemetry stream, rate limiter, or persistent log.

The `log_level` parameter drives the runtime threshold through a change
callback. An invalid value is rejected; if an out-of-range value reaches the
callback through a restored or remote representation, logging falls back to
the compiled minimum.

## Parameter architecture

Parameters are named, typed runtime values with descriptions and flags. Each
semantic component owns its definitions, backing storage, validation, and
change callbacks. The executable composition passes the enabled definition
sets to `kfsw_param_init()`; the PARAM core validates them once and builds a
bounded, ID-sorted index. Applications read and write that index through
`include/kfsw/services/parameter.h`; the optional persistence and CSP adapters
consume the same index.

```text
application identity   logging service   boton_test module   test support
  node_id set           log_level set     live status set     fixture set
       \                     |                  |                 /
        +---------------- executable composition ---------------+
                                  |
                        kfsw_param_init(sets, count)
                                  |
                   PARAM core: validate + sorted index
                           /                    \
                 KPAR v1 persistence       CSP/libparam adapter
```

The dependency direction is from an owner to the PARAM declaration API.
PARAM does not include owner headers or contain owner-specific IDs, ranges, or
callbacks. Adding an optional component requires adding its definition set to
the composition; it does not require editing the PARAM core.

The Kconfig split is central to the design:

```text
                         KFSW_PARAM
                    local table and API
                      /             \
                     /               \
                    v                 v
    KFSW_PARAM_PERSISTENCE       KFSW_PARAM_CSP
       requires KFSW_STORAGE     requires KFSW_CSP
       local snapshots           remote server/client
                    \                 /
                     \               /
                    may be selected independently
```

In dependency form:

```text
KFSW_PARAM_PERSISTENCE -> KFSW_PARAM + KFSW_STORAGE
KFSW_PARAM_CSP         -> KFSW_PARAM + KFSW_CSP
KFSW_FTP               -> KFSW_STORAGE + KFSW_CSP
```

`KFSW_PARAM` does not select or depend on CSP. The local parameter and
persistence unit suites build with `CONFIG_KFSW_CSP=n`, and
`tests/param-local-smoke.sh` provides an additional local-only integration
composition.

### Tables

A parameter is addressed by **table and offset**, not by a flat identifier.
The band a table sits in says who owns it, so two independently developed
components cannot be given the same table by accident:

| Band | Owner | Meaning |
| --- | --- | --- |
| 0 | — | Reserved invalid. A zero table identifier is never valid, so an uninitialised field cannot address a real table. |
| 1–24 | composition, platform, comms | Core: identity, links and hardware. |
| 25–49 | `kfsw-services` | One table per service. |
| 50–99 | `kfsw-modules` | Devices and subsystems. |
| 100–255 | — | Unallocated; left for mission payloads. |

The wire identifier carries the table in its high byte and the offset in its
low byte. That keeps it unique across the node, which is what the libcsp
parameter list requires, while decoding back to the table and offset an
operator reads. Offsets are unique inside a table; names are unique across the
node and are at most `KFSW_PARAM_NAME_MAX` (32) characters, refused at
registration rather than truncated.

A table is a definition set: the two are the same thing because a table is
owned by exactly one component, the one that can validate and apply its values.

Registered tables in the reference composition:

| ID | Band | Name | Owner |
| --- | --- | --- | --- |
| 1 | core | `board` | `k-fsw/app/src/parameters/board_table.c` |
| 2 | core | `system` | `k-fsw/app/src/parameters/system_table.c` |
| 3 | core | `telemetry` | `k-fsw/app/src/parameters/telemetry_table.c` |
| 4 | core | `csp` | `k-fsw/app/src/parameters/csp_table.c`, with CSP |
| 5 | core | `storage` | `k-fsw/app/src/parameters/storage_table.c`, with storage |
| 6 | core | `watchdog` | `k-fsw/app/src/parameters/watchdog_table.c`, with the watchdog |
| 24 | core | `test` | `tests/support/parameter_definitions.c`, opt-in fixtures |
| 25 | service | `log` | `kfsw-services/src/log.c` |
| 67 | module | `hw_test` | `kfsw-modules/boton-test` |

The core tables live in the composition layer rather than in `kfsw-platform`
and `kfsw-comms`. Those layers sit below the parameter service and never
include a services header, so a table there would invert an established
dependency. Each core table reads its values through the public API the layer
below already exposes, which leaves the state where it belongs.

### Write modes

A parameter's write behaviour is part of its contract. The mode is derived from
the definition rather than declared separately, so it cannot drift from what
the code does:

| Mode | Meaning | How it is built |
| --- | --- | --- |
| `r` | Read-only | `KFSW_PARAM_FLAG_READ_ONLY` |
| `w` | Takes effect immediately | a `changed` callback, or `KFSW_PARAM_FLAG_LIVE` where the owner reads the value every cycle |
| `b` | Stored; the running system keeps its old value until reboot | `KFSW_PARAM_FLAG_PERSISTENT` with neither of the above |
| `wb` | Both | |

The service always sets `KFSW_PARAM_FLAG_LIVE` for a definition that supplies a
change callback, so a parameter that applies its value cannot be reported as
needing a reboot. A `b` parameter says so on write rather than acknowledging
with a bare `OK`: a stored value the operator believes is live is the failure
this scheme exists to prevent.

### Strings

`KFSW_PARAM_STRING` carries text up to `CONFIG_KFSW_PARAM_STRING_MAX` bytes
including the terminator. A definition declares the storage it owns through
`capacity`; a write longer than that is refused rather than truncated, because
a truncated value is a different value and the operator is never told. Strings
have their own validator and change callback, since neither can be passed
through the scalar union.

Values travel on the caller's stack in a bounded buffer rather than through an
allocator: a parameter service that allocated would have to fail at the worst
moment.

### Sampled values

A definition may supply a `sample` callback, which refreshes the backing store
immediately before a read. Live housekeeping uses it because a value is worth
reading only if it is current when it was asked for; an uptime refreshed on a
timer is wrong by up to one period every time somebody reads it.

The CSP server hands libparam the backing storage directly rather than going
through that read path, so it refreshes every sampled parameter before serving
a request. Without that, a remote read answers with whatever the storage last
held, which for a value nothing writes locally is its compiled default
forever: an uptime always zero, an identity always empty.

Sampling runs while PARAM serializes access, so a `sample` callback must not
call back into the parameter API.

### Transport sizing

Listing a table sends one packet per parameter, which makes it the largest
burst the composition produces. Three limits have to cover it and each fails
differently when it does not:

| Limit | Symptom when too small |
| --- | --- |
| `CSP_BUFFER_COUNT` | Every buffer sits on a connection's receive queue, the interface has none left to assemble the next frame, and the transfer stops outright |
| `CSP_CONN_RXQUEUE_LEN` | Packets past the queue depth are dropped, so the caller gets a list that looks complete and is not |
| `CSP_QFIFO_LEN` | The same, one layer lower, at the router's input |
| `CONFIG_KFSW_PARAM_REMOTE_POOL_SIZE` | The download stops part way with `-ENOSPC` |

The K-FSW compositions set all of them from
`CONFIG_KFSW_PARAM_MAX_DEFINITIONS`. libcsp's own defaults of 15 and 16 are
sized for ping-sized traffic.

`CONFIG_KFSW_PARAM_LIST_RDP` puts the list on reliable delivery, on by
default. The list has no acknowledgement of its own, so over a radio a lost
descriptor leaves a hole the caller cannot see.

### Local parameters

Software and physical test configurations may enable
`CONFIG_KFSW_PARAM_TEST_DEFINITIONS`, which contributes table 24 `test`. These
are not production parameters, but the table identifier stays reserved.

`CONFIG_KFSW_PARAM_MAX_DEFINITIONS` (default 64) bounds parameters across every
table and `CONFIG_KFSW_PARAM_MAX_TABLES` (default 16) bounds the tables
themselves. Registration is refused once either is full rather than overrunning
it.

The two button entries reference the same individually aligned `uint32_t`
backing fields represented by `kfsw_boton_test_get_status()`; PARAM does not
maintain a second copy. The typed API uses the module mutex to return a coherent
pair, while PARAM reads each scalar independently under its own table lock.
On the tested targets those naturally aligned U32 views are single-copy, but
the raw-value model does not provide a shared formal C synchronization edge
with the owner mutex. They are runtime observations rather than configuration,
so remote or local `set` operations are rejected and the persistent flag is
absent. `param save`, `param load`, and `param defaults` therefore never change
or restore them. PARAM now has an owner-read callback -- see **Sampled values**
above -- so if formal owner synchronization is required for these scalar reads,
that is where it belongs.

The three LED entries are non-persistent developer controls. Their validators
accept only `0` or `1` and call the same owner setter used by the shell. A GPIO
failure rejects the write before PARAM stores the new value, so reported owner
state remains truthful. The five values form the logical `hw_test` definition
set, registered as table 67 in the module band. The three LED offsets are
0x08, 0x09 and 0x0a; the two counters are at 0x00 and 0x04.

The public type enumeration names unsigned, signed, hexadecimal, float,
double, string, and data categories. The current local core accepts scalar
integer/hex/float/double sizes; string, data, and arrays are not implemented as
local values. The production and test definition sets use only the scalar
types described above.

`kfsw_param_init()` rejects an empty or malformed table, unsupported entry
type/shape, duplicate ID, or duplicate name. Local reads and writes are
serialized by a mutex. A write must match the entry's exact type and scalar
size, pass its current validation, and not target a read-only entry.

### Runtime values and compiled defaults

A compiled default initializes RAM when the image starts. A normal `set`
changes RAM immediately and invokes the entry callback, but does not change
the compiled image or saved snapshot.

```text
compiled default --boot--> runtime value --set--> new runtime value
       ^                         |
       |                         +-- save --> persistent snapshot
       |
  param defaults
```

`param defaults` restores persistent entries to their compiled values in RAM.
It deliberately leaves the saved snapshot untouched; a later `param load` can
reapply it. `param clear` does the inverse: it removes saved state but leaves
current RAM values untouched.

### Local operations

The direct service API provides composition-aware initialization, state check,
get, set, and table visitor operations. The shell exposes the same distinction:

```text
kfsw:~$ param list
kfsw:~$ param get log_level
kfsw:~$ param set log_level 2
```

The shell parses text according to the parameter's actual type. Negative text
for an unsigned value, integer overflow, an invalid float, a missing name, and
a read-only write all produce errors rather than implicit conversion.

### Optional remote adapter

`CONFIG_KFSW_PARAM_CSP` adds `parameter_csp.c` and the selected subset of
[libparam](https://github.com/spaceinventor/libparam). It registers transaction
and parameter-list endpoints for the local table, and provides a client that
can download and cache a remote node's descriptions before get/set operations.

```text
local caller or shell
       |
K-FSW remote parameter API
       |
preallocated remote descriptor cache
       |
libparam request/response codec
       |
CSP ports 10 and 12
       |
remote adapter -> same remote local table
```

The default remote pool has 16 descriptors and is selected by
`KFSW_PARAM_REMOTE_POOL_SIZE`; it does not grow dynamically. Remote operations
use an explicit node and a bounded timeout. The adapter does not own CSP
initialization, interfaces, routes, or the router.

Current remote shell forms are:

```text
kfsw:~$ param list 2
kfsw:~$ param get 2 log_level
kfsw:~$ param set 2 log_level 2
```

When the NUCLEO button example is selected, the same generic adapter makes its
live owner state observable without adding CSP knowledge to the module:

```text
kfsw:~$ param get 2 boton_test_press_count
kfsw:~$ param get 2 boton_test_last_press_s
kfsw:~$ param set 2 hw_test_led_green 1
kfsw:~$ param get 2 hw_test_led_green
kfsw:~$ param set 2 boton_test_press_count 100
set: parameter 'boton_test_press_count' is read-only or service is not ready
```

The rejected write leaves both fields unchanged. Routing and the physical link
are composition concerns; `boton_test` depends only on the local PARAM
declaration API and the platform monotonic-time API.

The CSP adapter is protocol compatibility, not shared memory. Each node owns
its local table and applies its own validation/callbacks when a remote write is
decoded. An invalid externally supplied value is rejected by the owner
validator and the parameter is restored to its compiled default; it does not
retain the last runtime value.

## Parameter persistence

### Lifecycle and intent

Persistence stores selected local parameters in one versioned snapshot. It is
explicit so an operator can test a runtime change before deciding that it
should survive reboot.

```text
                 compiled default
                        |
                       boot
                        |
              initialize local table
                        |
             valid snapshot available?
                  /             \
                yes              no/error
                 |                  |
         restore saved values   keep defaults
                  \             /
                    runtime RAM
                        |
                       set
                        |
                 changed RAM value
                        |
                  explicit save
                        |
             atomic snapshot replacement
                        |
                    next boot
```

Only entries with the K-FSW persistent flag are saved. The read-only
`node_id` is excluded. The active file is
`/kfsw/params/parameters.dat`; an in-progress save uses
`/kfsw/params/parameters.tmp`.

### Snapshot format and validation

The bounded snapshot is at most 256 bytes and contains a 20-byte header plus
up to 16 typed entries. The header carries:

- magic `KPAR`;
- format version 1;
- header and payload sizes;
- entry count;
- reserved fields that must be zero; and
- IEEE CRC32 over the complete snapshot with the CRC field cleared.

Each entry contains a bounded name, K-FSW persistence type, value length, and
big-endian value bytes. The currently persistent types are `u8`, `u32`, `i32`,
and 32-bit float.

Load first checks file type and total bounds, reads the complete snapshot,
validates its structure and CRC, then applies entries. Unknown names and known
names with incompatible types are ignored so a newer/older image can retain
the values it understands. A bad magic, unsupported version, malformed length,
excess count, truncated entry, or CRC mismatch rejects the snapshot before it
changes the live table.

On boot, rejection is logged and compiled defaults stay active. The filesystem
is not reformatted and startup continues to `@READY`.

### Atomic replacement

A save follows this sequence:

```text
snapshot RAM
    |
write parameters.tmp
    |
fs_sync temporary file
    |
close successfully
    |
LittleFS rename temporary -> parameters.dat
    |
new snapshot is active
```

If write, sync, close, or rename fails, K-FSW removes the temporary file and
returns the error. The previous active snapshot is not deliberately truncated
as the first step. At load time, an abandoned temporary file is removed before
the active file is examined.

CRC catches accidental corruption and incomplete/malformed content; it is not
authentication. Temporary-file replacement limits the window in which a reset
can leave no complete snapshot. LittleFS supplies the filesystem-level
power-loss behavior, while K-FSW supplies the application record validation.

### Operator semantics

| Command | RAM after command | Saved snapshot after command |
| --- | --- | --- |
| `param set <name> <value>` | Changed | Unchanged |
| `param save` | Unchanged | Replaced from persistent RAM entries |
| `param load` | Updated from valid compatible entries | Unchanged |
| `param defaults` | Persistent entries reset to compiled defaults | Unchanged |
| `param clear` | Unchanged | Active and temporary files removed |

Persistence is local. A remote `param set 2 ...` changes node 2's RAM; it does
not implicitly run `param save` on node 2.

## Embedded storage and LittleFS

### Why a filesystem is present

Microcontroller flash has erase-block and write constraints and can lose power
during an update. [LittleFS](https://github.com/littlefs-project/littlefs) is a
small embedded filesystem designed for bounded RAM, wear distribution, and
power-loss resilience. Zephyr integrates it with its flash-map and filesystem
APIs; K-FSW owns when the selected volume is initialized and mounted.

The complete upstream references are the
[LittleFS project](https://github.com/littlefs-project/littlefs) and Zephyr's
[filesystem documentation](https://docs.zephyrproject.org/4.4.0/services/file_system/index.html).

K-FSW mounts one volume at `/kfsw`. The platform layer exposes init, mount,
unmount, readiness, backend identity, and capacity. Services use normal Zephyr
filesystem operations after readiness is established.

### Flash layout

The storage backend is selected by the `kfsw,storage-partition` devicetree
chosen property. Reusable code never contains a board-specific address.

NUCLEO-L496ZG uses the final 64 KiB of its 1 MiB internal flash:

```text
NUCLEO-L496ZG internal flash

0x00000000                                            0x00100000
     |-----------------------------------------------------|
     | K-FSW application partition | LittleFS storage      |
     | 0x00000000 + 0x000F0000     | 0x000F0000 + 0x10000 |
     |          960 KiB            |        64 KiB         |
     |-----------------------------------------------------|
                                   ^
                     kfsw,storage-partition
```

`CONFIG_USE_DT_CODE_PARTITION=y` makes the application linker respect the
code partition instead of occupying the storage region.

The native simulator selects a separate 64 KiB flash region at offset
`0x000FC000`. Zephyr backs simulated flash with a host file, normally
`build/linux/kfsw-storage.bin` through the K-FSW runner. This intentionally
exercises the same flash-map, LittleFS, and Zephyr filesystem code as the MCU
composition rather than replacing it with `fopen()`.

FRDM-K64F and Pico W shell profiles disable storage and do not define a K-FSW
storage partition.

### Mount and format policy

K-FSW first mounts with `FS_MOUNT_FLAG_NO_FORMAT`. If that succeeds, the
volume becomes ready. If LittleFS reports the specific corrupt/unformatted
condition, the platform scans the entire fixed partition through the flash-map
API.

```text
mount without format
       |
       +-- success ------------------------> ready
       |
       +-- failure other than EFAULT ------> report error
       |
       +-- EFAULT -> scan every flash byte
                         |
                         +-- all erased -> allow one format + mount
                         |
                         +-- any non-erased byte -> report error
```

This policy distinguishes factory/first-boot media from non-empty media that
may contain recoverable data. K-FSW does not silently erase a corrupt,
non-erased partition. A mount failure leaves storage unready; dependent
services report their own initialization errors.

## K-FSW FTP

### What it is—and is not

K-FSW FTP is the project's file-transfer service. It is not compatible with
Internet FTP, FTPS, SFTP, or TFTP. The name describes its role; the wire
protocol is a compact K-FSW request/data/result protocol carried in CSP
datagrams.

It has client and server APIs for list, stat, mkdir, upload (PUT), and download
(GET). The default server listens on CSP port 9 and requires both RDP and CSP
CRC32. Each protocol message has a 24-byte header, bounded path/data fields,
request ID, offset, total size, and file CRC where applicable.

### The local node

`kfsw_ftp_list`, `kfsw_ftp_stat`, and `kfsw_ftp_mkdir` accept this node's own
CSP address. Those calls run the same filesystem operations the server runs for
a decoded request, directly against local storage. No connection is opened and
no route is consulted, so a node can always inspect and prepare its own FTP
root — including on a node with no configured peer.

Local requests need storage mounted and the service started; otherwise they
return `-EACCES`. `kfsw_ftp_put` and `kfsw_ftp_get` move a file between two
nodes and return `-ENOTSUP` for the local address.

This is a deliberate short circuit in the service, not CSP loopback. Self
addressing at the CSP layer is not part of the verified routing scope, and
nothing in K-FSW depends on it.

### Internal layering

The service separates what a transfer means from how its messages travel:

```text
        client and server operations
   operation state, request/response order,
        which file each request touches
                    |
             transfer engine
   one send loop, one receive loop, shared by
    both roles; owns the open file handle
                    |
        reliable transport (ftp_link.h)
   connect, listen, accept, send, receive,
          release, close, max payload
                    |
              CSP with RDP and CRC32
                    |
             CSP router, KISS, UART
```

Only the transport backend includes libcsp. The operation and engine layers
work in terms of protocol messages and borrowed receive frames, so packet
ownership is expressed by the interface rather than by convention:
`kfsw_ftp_link_receive()` hands back a frame whose `path` and `data` point into
the transport's buffer, and `kfsw_ftp_link_release()` is what ends that borrow.

Two leaf units sit beside those layers and call nothing above them: the wire
codec with path policy and status mapping, and the storage rules for whole-file
CRC, the temporary file, and the atomic commit.

RDP already guarantees ordering and retransmission, so the application does not
add a second acknowledgement layer. The `offset` field in every data message is
a consistency assertion that a stray or replayed packet cannot advance the
write; it is not a reordering mechanism.

The usable payload is bounded by the CSP buffer minus the RDP header, the
CRC32, and the file-transfer header. `kfsw_ftp_link_max_payload()` reports that
limit, and a build assertion holds the compiled chunk size below it.

### Virtual paths and sandbox

Every FTP path is virtual and rooted below `/kfsw/ftp` on the node that uses
it:

```text
FTP virtual path             Zephyr filesystem path
----------------             ----------------------
/build/sample.bin     ->     /kfsw/ftp/build/sample.bin
/exchange/result.dat  ->     /kfsw/ftp/exchange/result.dat
```

Virtual paths are limited to 96 bytes. The validator rejects relative paths,
empty components, `.` and `..` traversal, backslashes, control characters,
embedded NULs, and overlong names. A client cannot name
`/kfsw/params/parameters.dat` through the FTP service because mapping always
adds the FTP root.

The sandbox is a namespace and accidental-traversal boundary. It is not user
authentication or access control.

### Chunks and integrity

PUT and GET stream file data in chunks of at most 192 bytes. Application-level
offsets require each chunk to arrive at the expected location. The sender
provides total file size and an IEEE CRC32; the receiver recomputes both and
commits only an exact match.

CSP CRC32 and file CRC serve different scopes:

- CSP CRC32 checks one transported CSP packet.
- the FTP file CRC checks the complete reconstructed file.
- RDP provides ordered/retransmitted delivery between endpoints.
- the `.part`/rename rule protects the local final pathname.

None of these provides cryptographic authenticity.

### Upload flow

```text
client                                      server
  |                                           |
  |-- connect CSP/RDP+CRC32 to port 9 -------->|
  |-- PUT request: path, total size, CRC ----->|
  |<----------------------------- PUT_READY ---|
  |-- DATA offset 0, <=192 bytes ------------->|
  |-- DATA next offset, <=192 bytes ---------->|
  |                 ...                       |
  |                                  write path.part
  |                                  sync + close
  |                                  verify size + CRC
  |                                  rename .part -> path
  |<--- PUT_RESULT: status, size, CRC ----------|
  |-- close ----------------------------------->|
```

A failed upload removes the partial file. An existing final file is preserved
unless a complete, validated temporary file reaches the rename step.

### Download flow

```text
client                                      server
  |                                           |
  |-- connect CSP/RDP+CRC32 to port 9 -------->|
  |-- GET request: remote path --------------->|
  |<--- GET_INFO: total size, CRC --------------|
  |<--- DATA offset 0, <=192 bytes -------------|
  |<--- DATA next offset, <=192 bytes ----------|
  |                 ...                       |
  |<--- GET_RESULT: status, size, CRC -----------|
  | write local-path.part                     |
  | sync + close; verify size + CRC           |
  | rename .part -> local path                |
```

Both sides therefore use atomic finalization when they are receiving a file.

### Concurrency and limits

The current server has one static acceptor and one static worker. It listens
with a one-connection backlog and returns busy for an overlapping connection
rather than allocating unbounded workers. The client uses one static workspace
guarded by a mutex, so client calls are serialized. Each protocol receive has
a configured timeout, currently 15 seconds by default.

The shell's `ftp generate` diagnostic is limited to 32768 bytes, but that is a
test-data limit, not the protocol's general file-size claim. Actual transfers
remain bounded by 32-bit protocol sizes, filesystem capacity, CSP resources,
timeouts, and operational link conditions.

### Verification scope

Software tests cover protocol encoding/decoding, path validation, CRC and
atomic-commit behavior, missing files, traversal rejection, zero-byte through
8 KiB transfers, byte comparison, and buffer recovery. Two-node ground roles
round-trip a file through `tests/k-ground-ftp-smoke.sh`. The NUCLEO physical
UART bench transfers and verifies 4 KiB and 16 KiB files in both directions of
the client workflow.

A 256-byte file has also been round-tripped over the Holybro SiK 433 MHz bench
between ground node 16 and NUCLEO node 2, with matching CRC on both nodes and
clean KISS counters. That is one small file on one named bench; it is not a
throughput characterisation, and no larger transfer over RF is claimed.

Browse @ref kfsw_services for exact public service declarations and return
contracts.

## Command service

`CONFIG_KFSW_COMMAND` enables one normalized path for invoking a K-FSW
operation. Before it existed, every remotely reachable operation had to supply
its own CSP adapter: parameters on port 10, file transfer on port 9, each with
a separate wire format, validation and error mapping. A third operation would
have meant a third protocol.

### One definition, two front ends

A command is defined once by its owning component and carries both a stable
text name and a stable numeric identifier:

```text
shell           "cmd info"           resolves by name
ground station   id 2 on CSP port 11 resolves by identifier
                         |
                  the same definition
                  the same validation
                  the same handler
```

The shell adapter implements no command. It converts text into the argument
types the definition declares and calls the same entry point the remote front
end uses, so a local operator cannot bypass a check a remote caller passes
through.

### Registry

Definitions arrive as compile-time sets from their owning component and the
registry is frozen at startup, the same shape as parameter definitions.
Startup rejects duplicate identifiers, duplicate names, missing handlers and
argument counts above the bound. There is no runtime registration: a command
that exists after startup existed at build time.

Handlers run on the command server thread, never on a CSP receive context, and
one invocation is serialized against another.

### Wire format

Requests use configurable CSP port 11 with CRC32, distinct from file transfer
and the parameter adapter. Each message has a twelve-byte explicit big-endian
header carrying version, opcode, status, argument count, command identifier,
request identifier and payload size. Arguments follow as bounded
type-length-value entries, and every length is checked against the buffer
before use. A build assertion holds one message inside one CSP packet.

### Security boundary

There is no authentication. The request context carries the source node and an
authentication result that is always false. The field exists so that adding
authentication later does not change the structure's meaning, and so no handler
encodes an assumption that a particular node is trusted. See
@ref kfsw_services_command for the public contract.

### Scope

Version 1 is synchronous: a handler runs to completion and returns a status
with an optional short detail. There is no accepted-plus-identifier form for a
long operation, no operation tracking and no duplicate suppression.

Remote parameter access deliberately has no command. The parameter service
already owns that path over CSP, and a second route to the same operation would
split its validation.

## Event record

`CONFIG_KFSW_EVENT` enables a bounded record of what a node has done.

Logging is a human-readable stream that exists only while someone is watching
it. Over a radio link, or after an unattended restart, it establishes nothing.
An event is a numeric record instead: a stable identifier, a monotonic
timestamp, a sequence number, a severity and a small opaque payload.

Events do not replace logging. A message that only helps a developer reading a
terminal stays a log call. A fact an operator may need after the moment has
passed becomes an event.

### Why numeric

```text
log    "FTP put node=2 destination=/uplink/test.txt: PASS bytes=256 crc32=0ce9d363"
event  source=ftp id=1 payload={node:2, bytes:256, crc32:0x0ce9d363}
```

Three properties follow from the second form: it is small enough to downlink
over a constrained link, a gap in the sequence is detectable, and rewording a
message does not break ground tooling because the identifier did not change.

### Ownership

Identifiers belong to the producing component and are declared in its own
public header together with the payload layout, so the event service does not
know its producers. Boot records the reset cause, the command service records
every dispatch outcome, and file transfer records completion and failure.

Payload bytes are opaque to the service and stored exactly as given. A producer
whose payload crosses a link writes it in network byte order.

### Bounds and loss

The record is a fixed RAM ring sized by Kconfig. When it wraps, the oldest
record is overwritten and an overwritten counter increases, so losing history
is visible rather than silent.

Emitting takes a short spinlock rather than a mutex, so any context can record
without sleeping, and the visitor runs outside the lock so a slow reader cannot
hold off a producer. Nothing in the service touches a link or a filesystem.

### Reading it remotely

A remote node's record is read through the command service, using
`event_stats` and `event_tail`, rather than through a separate protocol.

### Scope

The ring is RAM and does not survive a reset. It answers what a node has done,
not what happened before it restarted. There is no persistent journal, rate
limiting, coalescing or downlink stream. Persisting the record is separate work.
