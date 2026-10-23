# Firmware update {#firmware_update}

[TOC]

Upload a signed image, reboot into it, then confirm it after checking the node.
The L4 swap profile restores the previous image if the trial is unconfirmed at
the next reset. F7 uses the A + Golden recovery arrangement described below.
The watchdog causes that reset only when it is configured and health
monitoring stops feeding it.

## Selecting the update paths

`CONFIG_KFSW_FWU` is the shared flash writer, transfer ownership and bootloader
backend. The front ends are selected separately:

| Selection | Configuration |
| --- | --- |
| Normal commands and reserved FTP upload | `CONFIG_KFSW_FWU_STANDARD=y`, `CONFIG_KFSW_FWU_LITE=n` |
| Lite only | `CONFIG_KFSW_FWU_STANDARD=n`, `CONFIG_KFSW_FWU_LITE=y` |
| Both | Both options enabled |
| Neither | Both options disabled; the full application emits a build warning |

Enable the shared backend for either path. Lite over CSP also needs
`CONFIG_KFSW_FWU_LITE_CSP=y`. Standard defaults on when the shared backend is
selected, preserving existing compositions; Golden profiles explicitly select
lite only. Generic FTP remains independent. In a lite-only build, `firmware.bin`
is an ordinary FTP filename, and the normal `fwu` shell group is absent.

Both update paths accept the same target-specific artifact bytes. MCUboot
images include the header, payload and signature TLVs; use `zephyr.signed.bin`.
Build a separate artifact for each target. Native tests prove transfer and
stored bytes; they do not execute a received MCU image.

## Permanent Golden replacement of A

The opt-in F746 recovery composition uses the existing FWU-lite CSP service.
Apply `f746-minimal.conf`, `f746-golden.conf`, then
`f746-golden-upload.conf`, with the shared `f746-recovery-flash.overlay`
and `f746-golden-upload.overlay`. Retain the selected link configuration.
The layout is boot 64 KiB, executable Golden 192 KiB, A 256 KiB and files
512 KiB. There is no B slot. This profile does not enable storage.

Only permanent Golden writes A. BEGIN erases all of A, destroying the previous
image. ABORT also erases A. An interrupted upload may leave A incomplete;
MCUboot must reject it and return to Golden. There is no previous-A rollback.
A complete valid signed A may boot after an independent reset before explicit
validation or activation: Golden writes A directly. These commands do not add
a boot gate to the approved A + Golden layout. Transfer state is volatile; after a restart
begin a new upload rather than resuming it.

VERIFY flushes flash and independently reads the declared artifact bytes,
checking their CRC32 and SHA-256. START_FLASHING requires that verification:
READY means the image is verified, with
`swap_scheduled=false`. It does not reset the board. Command a reset separately;
MCUboot verifies the signature before selecting A. CRC is not authentication.
Golden remains available for another upload if A is rejected. The existing L4
swap profiles retain their offset, trailer and trial rollback behavior.

The native FWU and FWU-lite suites include recovery variants. `tools/ci/unit.sh`
runs the recovery guard and restart check after Twister. For a focused run,
build `tests/unit/services_fwu` with recovery enabled, then run
`python3 tests/unit/services_fwu/check_recovery.py /tmp/<build-directory>`
to check rejected compositions and restart after a partial upload. The restart
process selects only the inspection test, which checks idle service state and
inherited partial bytes before any abort or BEGIN. These checks
use simulated flash; they do not establish signature enforcement or physical
Golden fallback.

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
ftp ls 2 /boot
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

Successful completion checks the IEEE CRC32 against received and independently
read stored bytes, flushes the image to flash and schedules a trial boot. FTP
retains its existing CRC-only wire metadata; content-based resume belongs to
FWU lite. Read it back through `/boot/firmware_2.bin` before
resetting the board.

## Boot, check, confirm

On the flight console:

```text
fwu status
reboot 0000
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

The NUCLEO updates over CAN with the MCUboot, update and CAN profiles combined,
and FWU lite if wanted. The exact build, signed with the same key as the
installed bootloader, and the upload, readback, revert and confirmation tests
are in [tests/hil/fwu/README.md](https://github.com/dgonzalez97/k-fsw/blob/main/tests/hil/fwu/README.md). The image to
send is `build/<target>/app/zephyr/zephyr.signed.bin`.

The host adapter and flight node use 500 kbit/s. The radio uses the same
services; match the UART baud rate at each radio end. When radio encryption is
enabled, establish both sessions with `comms uhf connect` before uploading. See
@ref communications for key setup. Firmware signatures and radio keys serve
separate purposes; keep separate keys for them.

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

## FWU lite

FWU lite uploads directly through CSP without an intermediate receiver file.
Its protocol is independent of the underlying CSP interface: the effective CSP
route selects CAN or UART/KISS. I2C requires a working CSP interface in the
communications layer; this update does not add one or automatic link switching.

On the Linux sending node, builds with `CONFIG_KFSW_FWU_LITE_HOST_FILES` can read
an absolute host path outside `/kfsw/`. Put the generated test artifact at the
project checkout root, keeping it out of Git:

```bash
cp /absolute/path/to/build/app/zephyr/zephyr.signed.bin ./firmware.bin
sha256sum ./firmware.bin
```

Then use the Linux KFSW console:

```text
fwu_lite send 2 /absolute/path/to/k-fsw/firmware.bin
fwu_lite status 2
fwu_lite validate 2
fwu_lite flash 2
```

`send` reports receipt only. It does not validate, activate or reset the target.
`validate` independently reads stored bytes and checks CRC32 and SHA-256,
leaving `verified`. `flash` requires that state and leaves `ready`; repeating
it returns the same outcome without scheduling or counting another update.
Check `swap_scheduled` and the target's boot arrangement before a separate reset.
MCUboot retains signature authority; the digest is content identity, not sender
authentication or pre-boot signature validation.

For a short contact, repeat the same `send` command after restoring the route.
The receiver must stay powered. The sender reconciles the artifact descriptor,
transfer identity and progress, checks the local prefix CRC, then continues
without erasing accepted bytes. A changed image or another sender is refused.
Timeout and retry exhaustion preserve the upload. Use `fwu_lite abort <node>`
from the owning sender to deliberately erase it before replacing the image.
Failed cleanup retains the identity so the owner can retry.

Status distinguishes `received` bytes, including the live RAM buffer, from
`committed` bytes physically written. A failed multi-buffer write can commit a
prefix without accepting the complete block; only `failed` may therefore report
more committed than received bytes. Status remains readable and the owner can
retry abort. Neither count means verified. Session state and
accepted RAM bytes do not survive a receiver restart; begin a fresh upload after
reset. No durable checkpoint or automatic failover is provided in this version.

Blocks have byte offsets and CRC32. Replies bind to the operation, request,
transfer and generation. Identical retries include the short final block;
changed duplicates and stale handles are rejected. Both ends use the same
configured block size, default 160 bytes. RDP remains optional and off by
default. Older lite wire formats are incompatible; update the sender and
receiver together. Firmware artifact format is unchanged.

A hardware sending node reads a file in its own filesystem. A receiving board
needs no filesystem staging for lite. FTP uses a file in the sending node's
filesystem, so stage the same root artifact there when testing the normal path.

### Hosted acceptance

The hosted fixtures run the production endpoints and compare the target slot
with the exact source bytes. Without an input override they create an ignored,
temporary test artifact at the checkout root. Use a signed target artifact for
the same-image check:

```bash
KFSW_FWU_TEST_IMAGE="$PWD/firmware.bin" tests/k-ground-fwu-lite-smoke.sh --lossy --contacts
KFSW_FWU_TEST_IMAGE="$PWD/firmware.bin" tests/k-ground-fwu-ftp-smoke.sh --lossy
```

Set `KFSW_CSP_VERSION=1` to repeat with CSP v1. Lite loss tests drop BEGIN and
final-block replies; contact tests interrupt three contacts while the receiver
stays running. These software checks provide no physical boot evidence.

## Related

- @ref targets has the flash map and MCUboot configuration.
- `tests/hil/mcuboot/rollback.sh`: bootloader revert and wrong-key tests.
