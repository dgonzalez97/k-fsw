# K-FSW Application

This directory selects the features, binds the hardware and starts the
services. Reusable code goes in the other repositories.

The core parameter tables are here because the platform and comms layers can't
depend on the parameter service. Shell commands parse arguments, call a service
and print the result.

## Communications

Kconfig selects the links; devicetree selects their devices. The application
starts the `kfsw-comms` router. A single UART uses `KISS` with a default route.
Multiple interfaces need named UARTs and a route table.

## Storage

The application enables the LittleFS support from `kfsw-platform` and mounts
the storage partition at `/kfsw`. The board overlays define the flash layout,
so the platform code has no flash addresses.

The STM32L496ZG has 1 MiB of flash with 2 KiB erase pages and 8-byte write
alignment. K-FSW uses the last 64 KiB (32 pages) for the filesystem:

| Offset | Size | Use |
| --- | ---: | --- |
| `0x00000000` | 960 KiB | Application |
| `0x000F0000` | 64 KiB | LittleFS |

Parameter snapshots, transferred files and housekeeping share those 64 KiB.
Check free space and the image size for the selected composition. The MCUboot
layout splits the start of flash into a boot partition and two image slots
but keeps the storage partition at the same address.

KFSW-Linux uses the same LittleFS code on native_sim's simulated flash.
`tools/run-linux.sh` keeps a flash file in the build directory, and tests use
their own temporary flash files.

## Persistent parameters

The snapshot is restored after table registration, before the CSP server
starts. A missing or invalid snapshot leaves the compiled defaults.

`/kfsw/params/parameters.dat` holds the persistent values with a versioned
header and a CRC32. A save writes and syncs `parameters.tmp` and renames it
over the active file. `param defaults` only changes RAM and `param clear` only
deletes the saved file.

## File transfer

File transfer starts after storage is mounted and the router is running, and
before `@READY`. It listens on CSP port 9 with RDP and CRC32.

Paths are virtual and rooted at `/kfsw/ftp`. The service creates
`/kfsw/ftp/build` as a local exchange directory, so `/build/sample.txt` is
`/kfsw/ftp/build/sample.txt` in the Zephyr filesystem. The host filesystem of a
native node can't be reached.

```text
ftp mkdir [node] <directory>
ftp ls [node] [directory]
ftp stat [node] <path>
ftp put <node> <local-path> <remote-path>
ftp get <node> <remote-path> <local-path>
```

Without a node, `mkdir`, `ls` and `stat` act on this node. Listing `/` also
shows `boot`, the firmware slots, and `hk`, the housekeeping files, when they
exist; both are read-only.

```text
ftp put 7 /build/sample.txt /flash/sample.txt
ftp ls 7 /flash
```

`ftp generate <path> <bytes>` creates a test file and
`ftp verify <first> <second>` compares two local files.

Transfers check size and CRC32 before replacing a file. The server handles one
transfer at a time; others get `busy`. See the
[service guide](../docs/services/index.md#file-transfer) for protocol limits.
