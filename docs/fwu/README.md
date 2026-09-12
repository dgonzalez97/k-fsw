# Firmware update {#firmware_update}

[TOC]

Upload a signed image, verify it, and let MCUboot boot it on trial.
Confirm it after checking the node; an unconfirmed image reverts on the next reset.

## What exists today

| Piece | State |
| --- | --- |
| Bootloader, A/B slots, automatic revert | working, physically verified |
| Update service: receive, verify, offer to the bootloader | working, 26 unit tests |
| Shell control: `fwu status` / `abort` | working |
| Carrying the image from ground, over FTP | working |
| Carrying it over CSP block by block (`fwu_lite`) | working |

An image is uploaded with the ordinary `ftp put` that already carries files,
addressed to a reserved name. Nothing about the wire protocol changed.

## Update sequence

```
ground                          flight
------                          ------
crc32 of image      ──begin──►  erase slot, start streaming
image bytes         ──write──►  write, accumulate crc32
                    ──finish─►  verify crc32, ask bootloader to try it
reboot              ─────────►  bootloader swaps and runs the new image
  new image confirms itself  →  permanent
  new image does not         →  bootloader puts the old one back
```

Revert requires another reset. A configured watchdog and health policy
provide that reset if the candidate stops responding.

## Building a node that accepts updates

```bash
cd /path/to/k-fsw-workspace
source .venv/bin/activate
P="$PWD/k-fsw/config/profiles"

KFSW_SYSBUILD=1 \
KFSW_MCUBOOT_KEY="$HOME/.config/kfsw/mcuboot-signing-key.pem" \
KFSW_EXTRA_CONF_FILE="$P/nucleo-mcuboot.conf;$P/nucleo-mcuboot-fwu.conf" \
KFSW_EXTRA_DTC_OVERLAY_FILE="$P/nucleo-mcuboot-flash.overlay;$P/nucleo-mcuboot.overlay;$P/nucleo-mcuboot-fwu.overlay" \
KFSW_MCUBOOT_DTC_OVERLAY_FILE="$P/nucleo-mcuboot-flash.overlay" \
  ./k-fsw/tools/build.sh nucleo_l496zg

west flash -d build/nucleo_l496zg
```

`KFSW_MCUBOOT_KEY` is not optional in practice. Leave it out and the bootloader
is built with the key MCUboot ships in its own public tree, which anyone can
sign an image with.

## Uploading an image

This example requires the signed image in the sending node's filesystem,
at `/kfsw/ftp/build/zephyr.signed.bin`. Shell FTP paths are relative to
`/kfsw/ftp`; a host path under `build/nucleo_l496zg/` is not a node file.
Use direct block upload below if the image will not fit in ground-node storage.

```
kfsw-gnd# ftp put 2 /build/zephyr.signed.bin /firmware.bin
```

`firmware.bin` is reserved. A put to that name is streamed into the firmware
slot instead of being stored as a file; any other name is an ordinary file
transfer. The name is `CONFIG_KFSW_FTP_FIRMWARE_PATH`.

FTP supplies the size and CRC32. The receiver streams directly into the
secondary slot without staging a file in its 64 KiB filesystem.

When the transfer completes the image has been verified and offered to the
bootloader:

```
kfsw:~$ fwu status
state: ready
received: 156760
expected_crc32: 9f3a2b1c
actual_crc32: 9f3a2b1c
swap_scheduled: yes
```

Then reboot, and confirm or reject as below.

## Choosing a route

Both routes end at the same update service and can be built together.
The first transfer owns the slot; competing uploads receive `busy`.

| | File transfer route | Direct upload (`fwu_lite`) |
| --- | --- | --- |
| Where the image is | a file on the sending node | a file on the sending node, or on its host |
| Reliability | the transport's, connection-oriented | per-block checksum and repeat |
| Retry scope | RDP retransmits; a failed transfer restarts | one block |
| Wire protocol | unchanged; an ordinary put | its own, twelve-byte header |
| Best when | there is somewhere to put a file and the link is good | the link is marginal, or the image is only on the host |

Damaged CSP packets may be dropped before reaching the service.
The direct route retries when a reply times out.

## Direct block upload

On a Linux ground node with `CONFIG_KFSW_FWU_LITE_HOST_FILES=y`, `fwu send`
can read a host file. Use an absolute host path outside `/kfsw/`:

```text
kfsw-ops# fwu send 2 /path/to/k-fsw-workspace/build/nucleo_l496zg/app/zephyr/zephyr.signed.bin
kfsw-ops# fwu flash 2
```

The file transfer route needs the image to exist as a file on the sending node
and runs over a reliable connection. `fwu_lite` sends blocks straight across
CSP, checks each one on arrival, and lets the sender repeat a block that did
not survive.

```
kfsw-ops# fwu send 2 /kfsw/ftp/build/image.bin
Sending /kfsw/ftp/build/image.bin to node 2; this takes minutes over a slow link
Image accepted and verified; 3 block(s) resent

kfsw-ops# fwu flash 2
Node 2 scheduled a swap; reboot it to try the image
```

A failed checksum rejects the block without advancing the transfer.
A missing reply causes a retry. If an acknowledgement is lost, the receiver
reports its next expected block so the sender can continue.

Check `blocks resent` when assessing link quality.

**Reliable delivery is off by default** (`CONFIG_KFSW_FWU_LITE_RDP`). Per-block
checks and repeats already recover losses, and a second retry layer underneath
brings connection state and timeouts that can stall a transfer on a marginal
link instead of naming the block that needs resending. Turn it on for a link
where reordering rather than loss is the problem.

**Sending stops at a verified image.** Committing it is `fwu flash`, a separate
command, so a node never boots something merely because it arrived.

Every block except the last must be full. A short intermediate block is rejected.

## Preparing an image on the ground

The CRC is **IEEE** — the same one Python's `zlib` computes. The tree also
contains libcsp's Castagnoli CRC32, which is a different algorithm; using it
here produces a number the flight side will never agree with.

```bash
python3 - <<'PY'
import zlib, pathlib
image = pathlib.Path("build/nucleo_l496zg/app/zephyr/zephyr.signed.bin").read_bytes()
print(f"size  {len(image)}")
print(f"crc32 0x{zlib.crc32(image) & 0xFFFFFFFF:08x}")
PY
```

Use `zephyr.signed.bin`, not `zephyr.bin`. The bootloader verifies a signature
before it will run anything, and an unsigned image is refused.

## Driving an update from the shell

```
uart:~$ fwu status
K-FSW firmware update
target: bound
state: idle
max_image_bytes: 352256
write_offset: 2048
...

uart:~$ fwu begin 156760 0x9f3a2b1c
Firmware update ready for 156760 bytes

   ... image bytes are written by the transport ...

uart:~$ fwu finish
Firmware update accepted; swap scheduled: yes
Reboot to try it. It reverts unless it is confirmed.

uart:~$ cmd reboot
```

After the reboot, the new image is running **on trial**:

```
uart:~$ mcuboot
swap type: revert
confirmed: 0
primary area (0):
  version: 2.0.0+0
```

`confirmed: 0` means that if the board reboots again without confirming, the
bootloader restores the previous image. To keep the new one:

```
uart:~$ mcuboot confirm
```

To reject it, just reboot without confirming.

## Checking it worked

Check `swap scheduled: yes`, reboot, then compare the running image version.
A completed transfer alone does not prove the new image booted.

## Loading a slot over ST-LINK

For bring-up, or when there is no link:

```bash
openocd -f zephyr/boards/st/nucleo_l496zg/support/openocd.cfg \
  -c "init" -c "reset halt" \
  -c "flash write_image erase build/.../zephyr.signed.bin 0x08068800" \
  -c "reset run" -c "shutdown"
```

`0x08068800` is `slot1` plus one sector. `0x08068000` would silently do nothing.

## If something goes wrong

| Symptom | Meaning |
| --- | --- |
| `begin` returns `-EFBIG` | the image is larger than the slot can hold; `fwu status` prints the maximum |
| `write` returns `-ESPIPE` | a chunk arrived out of order; the transport must send them in sequence |
| `finish` returns `-EAGAIN` | fewer bytes arrived than `begin` declared |
| `finish` returns `-EILSEQ` | the CRC32 does not match; the slot is erased so a partial image cannot be booted |
| `finish` returns `-EIO` | the bootloader did not schedule a swap — usually the wrong write offset |
| Board boots the old image after a reboot | the new one did not confirm itself, so the bootloader reverted. Working as designed |
| `ftp put` to `firmware.bin` reports a transfer error | the update service refused the image; `fwu status` gives the reason |
| `fwu send` reports `bad-block` repeatedly | the link is corrupting data faster than the retry count allows; raise `CONFIG_KFSW_FWU_LITE_BLOCK_RETRIES` or improve the link |
| `fwu send` reports `busy` | another upload is in progress, by either route; `fwu abort` on the receiving node clears it |
| `fwu send` reports `invalid` on the first block | the two ends disagree about the block size; they must be built with the same `CONFIG_KFSW_FWU_LITE_BLOCK_SIZE` |

`fwu abort` is always safe and leaves the slot erased.

A failed or abandoned upload always leaves the slot erased rather than holding
a partial image, so there is never a half-written image for the bootloader to
find.

## Related

- @ref targets — flash map and MCUboot profile.
- @ref testing — rollback acceptance.
- `tests/hil/mcuboot/rollback.sh` — revert, confirmation, wrong-key rejection.
