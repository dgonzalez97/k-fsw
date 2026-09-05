# K-FSW Application

Where the image is put together. This layer stays small on purpose: behaviour
belongs to the repository that owns it, not to `main.c`.

What lives here is the part that cannot live anywhere else — the core parameter
tables, because `kfsw-platform` and `kfsw-comms` sit below the parameter
service and must not depend on it, and the shell adapters, which parse
arguments, call a service API, and print.

## Communications

The application picks Kconfig values and devicetree UART instances, then starts
the single router that `kfsw-comms` owns. One chosen UART gives one interface
named `KISS` and a direct default route. A multi-link overlay declares any
number of named UART/KISS children and supplies a route table, which libcsp
parses and uses — route selection never happens in application code.

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

That is a conservative split: enough for parameter snapshots, transferred
files and staging metadata, without spending a large share of the device on a
filesystem. The application sits well below the 960 KiB ceiling.

This is the layout without a bootloader. The MCUboot composition rearranges the
front of the flash into a boot partition and two image slots and **leaves the
storage partition exactly where it is** — that is what lets an existing
filesystem, with its snapshots and files, survive the migration.

KFSW-Linux uses the same lifecycle, Zephyr flash map, and LittleFS code over
native_sim's simulated flash. `tools/run-linux.sh` gives each Linux instance a
persistent backing image under its build directory. Tests pass explicit
temporary flash images so parallel nodes do not share media and transient test
data is cleaned up.

## Persistent parameters

With parameters and storage both enabled, the snapshot is restored after the
tables are built and before the CSP server starts, so a remote reader never
sees a value that is about to change. A missing or invalid snapshot leaves the
compiled defaults in place and does not stop the application.

Writing a value never touches flash on its own. Saving is a separate,
deliberate act.

The single `/kfsw/params/parameters.dat` file contains explicitly selected
writable values, a versioned header, portable name/type/value entries, and an
IEEE CRC32. Saves write and sync `parameters.tmp` before LittleFS atomically
renames it over the active file. `param defaults` changes RAM only; `param
clear` deletes saved state only.

## CSP file transfer

File transfer starts after storage is mounted and the router is running, and
before `@READY`. It listens on CSP port 9 with RDP and CRC32, does not own a
second router, and does not care which transport is underneath it.

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
supported in version 1. This protocol is K-FSW's own and claims no wire
compatibility with anything else that uses the FTP or TFTP name.
