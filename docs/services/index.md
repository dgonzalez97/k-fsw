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
- a statically allocated local parameter table;
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

Parameters are named, typed runtime values with descriptions and flags. The
local table is statically allocated and validated once. Applications read and
write it through `include/kfsw/services/parameter.h`; the optional CSP adapter
translates the same table to the selected libparam protocol.

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

### Local table

The current table is small because several entries are integration values,
not a claim of a complete spacecraft configuration schema.

| ID | Name | Type | Compiled default | Access | Persistent | Validation/effect |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | `node_id` | `u16` | CSP address, or `0` without CSP | Read-only | No | Build-time identity only |
| 1 | `log_level` | `u8` | `CONFIG_KFSW_LOG_MIN_LEVEL` | Writable | Yes | Range 0–4; callback updates runtime logging |
| 2 | `test_u32` | `u32` | `42` | Writable | Yes | Full `u32` shell range |
| 3 | `test_i32` | `i32` | `-7` | Writable | Yes | Full `i32` shell range |
| 4 | `test_float` | `float` | `1.5` | Writable | Yes | Tokens fully consumed by the current `strtof()` parser |

The public type enumeration names unsigned, signed, hexadecimal, float,
double, string, and data categories. The current local core accepts scalar
integer/hex/float/double sizes; string, data, and arrays are not implemented as
local values. The shipped table uses only the five types shown above.

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

The direct service API provides initialize, state check, get, set, and table
visitor operations. The shell exposes the same distinction:

```text
kfsw:~$ param list
kfsw:~$ param get test_u32
kfsw:~$ param set test_u32 1234
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
kfsw:~$ param get 2 test_u32
kfsw:~$ param set 2 test_u32 1234
```

The CSP adapter is protocol compatibility, not shared memory. Each node owns
its local table and applies its own validation/callbacks when a remote write is
decoded.

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
8 KiB transfers, byte comparison, and buffer recovery. The NUCLEO physical
UART bench transfers and verifies 4 KiB and 16 KiB files in both directions of
the client workflow. These results apply to the current CSP/RDP UART bench, not
to an unimplemented radio or CAN link.

Browse @ref kfsw_services for exact public service declarations and return
contracts.
