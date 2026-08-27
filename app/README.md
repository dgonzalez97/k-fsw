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
