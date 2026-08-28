# Services {#services}

## Logging

The logging API provides error, warning, information, and debug calls plus a
runtime minimum level. Compile-time Kconfig can remove calls below the selected
minimum. The shell command `log test` emits one message at each compiled level
for integration diagnostics.

## Parameters

The parameter service wraps the project-selected subset of libparam behind
`include/kfsw/services/parameter.h`. The application validates a static local
table, optionally restores persisted values, registers CSP parameter
endpoints, and then starts routing.

The default table contains a read-only `node_id` plus writable
`log_level`, `test_u32`, `test_i32`, and `test_float` values. Remote operations
accept a node explicitly.

| Operation | Local form | Remote form |
| --- | --- | --- |
| List | `param list` | `param list <node>` |
| Read | `param get <name>` | `param get <node> <name>` |
| Write RAM | `param set <name> <value>` | `param set <node> <name> <value>` |

Parameter transactions use CSP port 10 and list descriptions use CSP port 12
in the current configuration.

## Parameter persistence

Persistence is explicit and keeps RAM state distinct from saved state.

| Command | Effect |
| --- | --- |
| `param save` | Atomically replace the saved snapshot with selected RAM values |
| `param load` | Load a valid snapshot into RAM |
| `param defaults` | Restore compiled defaults in RAM without deleting the snapshot |
| `param clear` | Delete saved state without changing RAM |

The snapshot is `/kfsw/params/parameters.dat`. It has a versioned header,
typed entries, and IEEE CRC32. Saves sync a temporary file before LittleFS
renames it over the active snapshot. Invalid headers, versions, lengths, or
CRCs leave compiled defaults active and do not prevent the application from
reaching `@READY`.

## Storage

Both supported targets mount LittleFS at `/kfsw` through `kfsw-platform`.
KFSW-Linux uses Zephyr native simulation's persistent flash file, not a
host-specific `fopen()` backend.

`storage info` reports the backend, mount point, readiness, total capacity, and
free capacity. `storage test` performs bounded create, write, read, overwrite,
and delete checks. Its `write` and `read` forms support the cross-process
persistence regression.

## FTP

The K-FSW file-transfer protocol uses configurable CSP port 9 with RDP and CSP
CRC32. It is specific to K-FSW and is not wire-compatible with other FTP or
TFTP implementations.

All virtual paths are sandboxed below `/kfsw/ftp`; `/build/sample.bin` maps to
`/kfsw/ftp/build/sample.bin` inside the Zephyr filesystem. Empty components,
traversal, backslashes, control characters, embedded NULs, and paths longer
than 96 bytes are rejected.

PUT and GET stream 192-byte chunks. Receivers write a `.part` file, sync and
validate its size and CRC, then atomically rename it. A failed transfer removes
the partial file and preserves an existing final file. The current server uses
one static worker and reports `busy` for an overlapping connection.

Browse @ref kfsw_services for the public service API map.
