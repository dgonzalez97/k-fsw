# K-FSW Application

This directory selects the features, binds the hardware and starts the
services. Reusable code goes in the other repositories.

The core parameter tables are here because the platform and comms layers can't
depend on the parameter service. Shell commands parse arguments, call a service
and print the result.

## Communications

The application sets the Kconfig values and the devicetree UARTs, then starts
the router from `kfsw-comms`. One chosen UART gives one interface named `KISS`
with a default route. A multi-link overlay declares several named UART/KISS
interfaces and a route table, which libcsp parses; the application doesn't
pick routes itself.

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

That is enough for parameter snapshots, transferred files and housekeeping
data, and the application is well below 960 KiB. The MCUboot layout splits the
start of flash into a boot partition and two image slots but keeps the storage
partition where it is, so the filesystem survives the change.

KFSW-Linux uses the same LittleFS code on native_sim's simulated flash.
`tools/run-linux.sh` keeps a flash file in the build directory, and tests use
their own temporary flash files.

## Persistent parameters

With parameters and storage enabled, the snapshot is restored after the tables
are registered and before the CSP server starts, so a remote reader never sees
a value that is about to change. A missing or invalid snapshot leaves the
compiled defaults and startup continues.

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
ftp <node> mkdir <remote-directory>
ftp <node> ls [remote-directory]
ftp <node> stat <remote-path>
ftp <node> put <local-path> <remote-path>
ftp <node> get <remote-path> <local-path>
```

The verb can also go first, `ftp put <node> ...`, which is the form Tab
completion shows. `ls` is the same as `list`:

```text
ftp 7 put /build/sample.txt /flash/sample.txt
ftp 7 ls /flash
```

`ftp generate <path> <bytes>` creates a test file and
`ftp verify <first> <second>` compares two local files.

Protocol version 1 uses paths of up to 96 bytes and 192-byte chunks. PUT and
GET check the size and CRC32, and the receiver renames the synced `.part` file
only after the check. A successful PUT replaces an existing file and a failed
one leaves it unchanged. The server handles one request at a time and answers
`busy` to the rest. Transfers can't be resumed, and the protocol is not
compatible with other FTP or TFTP implementations.
