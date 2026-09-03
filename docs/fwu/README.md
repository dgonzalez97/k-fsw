# Firmware update

How to put a new image on a K-FSW node and have the bootloader try it, with a
way back if it does not work.

## What exists today

| Piece | State |
| --- | --- |
| Bootloader, A/B slots, automatic revert | working, physically verified |
| Update service: receive, verify, offer to the bootloader | working, 26 unit tests |
| Shell control: `fwu begin` / `finish` / `abort` / `status` | working |
| **Carrying the image bytes from ground** | **not built yet** |

The last row matters: the service is complete and tested, but nothing yet
feeds it over the radio. Until that lands, an image reaches a slot only over
ST-LINK. See [Getting the bytes in](#getting-the-bytes-in).

## The shape of an update

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

The last two lines are the safety net, and they are the reason nothing else in
this design needs to be clever. An image that hangs is reset by the watchdog,
the reset lands in the bootloader, and the bootloader reverts.

## Building a node that accepts updates

```bash
cd ~/projects/K-FSW
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

`swap scheduled: yes` is the line to look for. It is not decoration: an image
written to the wrong offset leaves the bootloader with nothing to swap, and it
reports that only by quietly running the old image on the next boot. From the
ground a silent no-op looks exactly like a successful update, so the service
asks the bootloader whether a swap was actually scheduled and fails if it was
not.

## Getting the bytes in

Not built yet. The service takes bytes through `kfsw_fwu_write()`; what is
missing is something that receives them over CSP and calls it.

Two things constrain that work, both learned the hard way:

**An image cannot be staged as a file.** The application image is around
154 KB and the filesystem partition on the NUCLEO is 64 KB. The transport must
stream into the slot, not save and then copy.

**Writing to the slot start does nothing.** MCUboot's swap-using-offset mode
needs the image one sector in. The service already handles this — callers pass
offsets from zero — but any transport that bypasses the service and writes
flash directly must do the same, and confirm a swap was scheduled afterwards.

Until it lands, load a slot over ST-LINK:

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

`fwu abort` is always safe and leaves the slot erased.

## Related

- `docs/targets/index.md` — the flash map and the opt-in MCUboot profile
- `docs/testing/index.md` — the rollback acceptance and what it proves
- `tests/hil/mcuboot/rollback.sh` — revert, permanent upgrade, wrong-key refusal
