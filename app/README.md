# K-FSW Application

Embedded image composition root.

Keep this application layer intentionally small.

Functionality belongs in the reusable repositories rather than in main.c.

## Storage composition

The application enables the `kfsw-platform` LittleFS lifecycle and mounts the
selected fixed flash partition at `/kfsw`. Board overlays own the physical
layout; the platform implementation contains no raw flash address.

The STM32L496ZG has 1 MiB of internal flash with 2 KiB erase pages and 8-byte
write alignment. K-FSW reserves the final 64 KiB (32 erase pages, 6.25% of
flash) for the filesystem:

| Offset | Size | Current use |
| --- | ---: | --- |
| `0x00000000` | 960 KiB | K-FSW application partition |
| `0x000F0000` | 64 KiB | LittleFS storage partition |

This is intentionally conservative: it is large enough for small parameter,
FTP, staging-metadata, and log users without consuming a large fraction of the
device. The current application is far below the 960 KiB ceiling.

A future MCUboot/A-B layout can retain the storage location and use, for
example, a 32 KiB bootloader plus two 464 KiB image slots. That exact boot
layout is deferred until the MCUboot roadmap item, but all four boundaries are
2 KiB-aligned and the current image fits comfortably in either proposed slot.

KFSW-Linux uses the same lifecycle, Zephyr flash map, and LittleFS code over
native_sim's simulated flash. `tools/run-linux.sh` gives each Linux profile a
persistent backing image under its build directory. Tests pass explicit
temporary flash images so parallel nodes do not share media and transient test
data is cleaned up.

## Persistent parameters

Profiles with PARAM and storage enabled restore the service-owned snapshot
after the parameter table is initialized and before its CSP server starts. A
missing or invalid snapshot leaves compiled defaults active and does not stop
the application. Runtime `set` operations never write flash automatically.

The single `/kfsw/params/parameters.dat` file contains explicitly selected
writable values, a versioned header, portable name/type/value entries, and an
IEEE CRC32. Saves write and sync `parameters.tmp` before LittleFS atomically
renames it over the active file. `param defaults` changes RAM only; `param
clear` deletes saved state only.

## CSP file transfer

The application enables the K-FSW file-transfer service after storage is
mounted and the CSP router is running, before `@READY`. The service listens on
configurable CSP port 9 and requires libcsp RDP plus CSP CRC32. It does not own
another router and is independent of the underlying KISS/UART transport.

Remote and KFSW-Linux client paths are virtual paths below `/kfsw/ftp`. The
service creates `/kfsw/ftp/build` as the local exchange directory, so the
operator path `/build/sample.txt` refers to
`/kfsw/ftp/build/sample.txt` inside the Zephyr filesystem. It does not expose
the native host filesystem.

The primary shell syntax is:

```text
ftp <node> mkdir <remote-directory>
ftp <node> ls [remote-directory]
ftp <node> stat <remote-path>
ftp <node> put <local-path> <remote-path>
ftp <node> get <remote-path> <local-path>
```

The same root command also exposes Zephyr static subcommands in verb-first
form, such as `ftp put <node> ...`, so normal subcommand tab completion works.
`ls` aliases `list`. For example:

```text
ftp 7 put /build/sample.txt /flash/sample.txt
ftp 7 ls /flash
```

The debug shell also provides `ftp generate <path> <bytes>` for bounded,
deterministic test fixtures and `ftp verify <first> <second>` for a
byte-for-byte local comparison. These helpers still use the same Zephyr
filesystem and FTP sandbox.

Protocol version 1 uses a 96-byte maximum virtual path and 192-byte streaming
chunks. PUT/GET validate total size and IEEE CRC32, and receivers commit a
synced `.part` file with atomic rename only after validation. Successful PUT
replaces an existing final file; failed transfers preserve it. One request is
active per server, overlapping clients receive `busy`, and resume is not
supported in version 1. This K-FSW-owned protocol does not claim GomSpace or
other FTP/TFTP wire compatibility.
