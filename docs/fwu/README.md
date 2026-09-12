# Firmware update {#firmware_update}

[TOC]

Upload a signed image, reboot into it, then confirm it after checking the node.
MCUboot restores the previous image if the trial is unconfirmed at the next reset.
The watchdog provides
that reset only when it is configured and its health policy stops feeding it.

## Firmware files

With `CONFIG_KFSW_FWU_FILES`, FTP exposes both flash slots:

| FTP path | Contents |
| --- | --- |
| `/boot/firmware_1.bin` | Primary slot (`slot0`): the running image |
| `/boot/firmware_2.bin` | Secondary slot (`slot1`): the uploaded or previous image |

On the NUCLEO swap profile, MCUboot moves the candidate into the primary slot.
The previous image remains in the secondary slot until the next upload or
`fwu abort` erases it. These names identify slots, not firmware versions.

```text
ftp list 2 /boot
ftp get 2 /boot/firmware_1.bin /build/running.bin
ftp get 2 /boot/firmware_2.bin /build/previous.bin
```

The files read flash directly; they use no space in the board's LittleFS
partition. They contain the MCUboot header, payload and TLVs, without slot
padding or swap metadata. The local mount is `/kfsw/boot`.

Both files are read-only. A secondary-slot reader blocks upload, abort and swap
requests until it closes. An incomplete or failed upload is unavailable.
A readable image has passed structural checks; MCUboot checks its signature
before booting it.

## FTP upload

Place `zephyr.signed.bin` in the sending node's filesystem, then run:

```text
ftp put 2 /build/zephyr.signed.bin /firmware.bin
```

`/firmware.bin` is the reserved upload path
(`CONFIG_KFSW_FTP_FIRMWARE_PATH`). It writes the secondary slot directly.
FTP paths such as `/build/zephyr.signed.bin` are relative to `/kfsw/ftp` on the
sending node; they are not paths on its Linux host.

Successful completion checks the IEEE CRC32, flushes the image to flash and
schedules a trial boot. Read it back through `/boot/firmware_2.bin` before
resetting the board.

## FWU lite upload

FWU lite accepts a node file, or a host file on builds with
`CONFIG_KFSW_FWU_LITE_HOST_FILES`. Use an absolute host path outside `/kfsw/`:

```text
fwu send 2 /path/to/zephyr.signed.bin
ftp get 2 /boot/firmware_2.bin /build/candidate.bin
fwu flash 2
```

`fwu send` checks and flushes the image without scheduling a boot. The receiver
reports `verified`. `fwu flash` schedules the trial and changes it to `ready`.
Both upload paths leave the same slot file available for readback.

Blocks carry an IEEE CRC32. Lost replies are retried; all blocks except the
last must have the configured size. Both ends need the same
`CONFIG_KFSW_FWU_LITE_BLOCK_SIZE`. RDP is optional and off by default.

## Boot, check, confirm

On the flight console:

```text
fwu status
cmd reboot 0000
mcuboot
```

Check the running version over the link with `csp ident 2`. A completed upload
does not prove that the candidate booted. Check node health and read back the
previous image before accepting the candidate:

```text
mcuboot confirm
```

To revert, reset without confirming. Upload and abort requests return `busy`
while MCUboot needs the secondary image for trial rollback.

## CAN profile

Combine the MCUboot, update and CAN profiles for the NUCLEO. FWU lite is optional.
Use the same signing key as the installed bootloader.

```bash
cd /path/to/k-fsw-workspace
source .venv/bin/activate
P="$PWD/k-fsw/config/profiles"

KFSW_SYSBUILD=1 \
KFSW_MCUBOOT_KEY="$HOME/.config/kfsw/mcuboot-signing-key.pem" \
KFSW_EXTRA_CONF_FILE="$P/nucleo-mcuboot.conf;$P/nucleo-mcuboot-fwu.conf;$P/nucleo-mcuboot-fwu-lite.conf;$P/nucleo-can.conf" \
KFSW_EXTRA_DTC_OVERLAY_FILE="$P/nucleo-mcuboot-flash.overlay;$P/nucleo-mcuboot.overlay;$P/nucleo-mcuboot-fwu.overlay;$P/nucleo-can.overlay" \
KFSW_MCUBOOT_DTC_OVERLAY_FILE="$P/nucleo-mcuboot-flash.overlay" \
  ./k-fsw/tools/build.sh nucleo_l496zg
```

The host adapter and flight node use 500 kbit/s. See
`tests/hil/fwu/README.md` for upload, readback, revert and confirmation tests.
The radio uses the same services; match the UART baud rate at each radio end.

## Errors

| Result | Action |
| --- | --- |
| `busy` | Wait for the current upload or slot reader to finish |
| `-EFBIG` | Check `max_image_bytes` in `fwu status` |
| `-ESPIPE` | Send the next expected offset |
| `-EAGAIN` | Finish sending the declared image size |
| `-EILSEQ` | Check the whole-image IEEE CRC32 |
| `-EIO` when scheduling | Check MCUboot configuration and the write offset |
| `fwu abort` fails | Read the flash error and retry cleanup; the slot may still contain bytes |

`fwu abort` erases the secondary slot, including any previous image kept there.
A failed erase leaves the service in `failed` with its transfer details intact.

## Related

- @ref targets — flash map and MCUboot configuration.
- `tests/hil/mcuboot/rollback.sh` — bootloader revert and wrong-key tests.
