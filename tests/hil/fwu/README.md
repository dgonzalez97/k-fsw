# Firmware update over CAN

NUCLEO-L496ZG node 2, ground node 16, 500 kbit/s. Connect and terminate the
bench before starting. Keep the ST-LINK console available throughout the test.

`can-update.py` uses prebuilt images. It uploads a candidate through FTP,
reads both slots back, checks trial rollback, then repeats through FWU lite
and confirms the candidate. It refuses to start unless the initial image is
confirmed. Slot readbacks are compared byte for byte on the ground node.

The script does not build or install a bootloader. Running it changes the
firmware slots and reboots the board several times. The confirmed candidate
remains running at the end. Logs and the ground flash file are kept on failure.

## Build

From the workspace root, with the virtual environment active:

```bash
P="$PWD/k-fsw/config/profiles"
H="$PWD/k-fsw/tests/hil/fwu"
B="$PWD/build/fwu-can"

KFSW_BUILD_DIR="$B/ground" \
KFSW_EXTRA_CONF_FILE="$H/ground-can.conf" \
KFSW_EXTRA_DTC_OVERLAY_FILE="$H/ground-can.overlay" \
  k-fsw/tools/build.sh linux

KFSW_IMAGE_VERSION=fwu-can-after \
KFSW_SYSBUILD=1 \
KFSW_MCUBOOT_KEY="$HOME/.config/kfsw/mcuboot-signing-key.pem" \
KFSW_BUILD_DIR="$B/after" \
KFSW_EXTRA_CONF_FILE="$P/nucleo-mcuboot.conf;$P/nucleo-mcuboot-fwu.conf;$P/nucleo-mcuboot-fwu-lite.conf;$P/nucleo-can.conf" \
KFSW_EXTRA_DTC_OVERLAY_FILE="$P/nucleo-mcuboot-flash.overlay;$P/nucleo-mcuboot.overlay;$P/nucleo-mcuboot-fwu.overlay;$P/nucleo-can.overlay" \
KFSW_MCUBOOT_DTC_OVERLAY_FILE="$P/nucleo-mcuboot-flash.overlay" \
  k-fsw/tools/build.sh nucleo_l496zg
```

Build the baseline with `KFSW_IMAGE_VERSION=fwu-can-before` in `$B/before`.
Both images must include the slot-file support and use the same signing key.
These labels identify bench images; release images use their release version.

Install and confirm the baseline before running the test. Back up the board
first. Program only the bootloader and image slots; preserve the golden region
at `0x080c0000` and LittleFS at `0x080f0000`. Do not use a whole-chip erase.

## Run

```bash
sudo bash k-fsw/tests/hil/stm32/nucleo-l496zg/can-up.sh 500000 normal

python k-fsw/tests/hil/fwu/can-update.py \
  --ground "$B/ground/zephyr/zephyr.exe" \
  --image "$B/after/app/zephyr/zephyr.signed.bin" \
  --revision fwu-can-after \
  --serial "$KFSW_DEBUG_SERIAL" \
  --interface can0 \
  --output "$B/run-1"
```

`KFSW_DEBUG_SERIAL` must be the ST-LINK `/dev/serial/by-id/...` path. The output
directory must be new. This fixture's ground overlay provides 2 MiB of LittleFS
space for the candidate and readbacks; the board's storage geometry is unchanged.
Use `--reboot-pin` if the board's PIN differs from `0000`. The ground profile
uses 1,000 clock ticks per second for SocketCAN's transmit-completion polling.

## Timing and interrupted uploads

Add `probes.conf` to the flight build, then install and confirm that image.
The probes are excluded from normal builds. They measure wake-to-run latency,
full HK collection time, local PARAM reads, and PARAM sampling.

```bash
python k-fsw/tests/hil/fwu/can-acceptance.py \
  --ground "$B/ground/zephyr/zephyr.exe" \
  --image "$B/after/app/zephyr/zephyr.signed.bin" \
  --flash "$B/run-1/ground-flash.bin" \
  --serial "$KFSW_DEBUG_SERIAL" \
  --seconds 3600 --interruptions --output "$B/acceptance-1"
```

The load combines local and remote HK at 1 Hz, PARAM requests, file round trips,
and slot downloads. `timing.txt` records observed maxima, not a WCET proof.
The fixture rejects a run with no measured HK collections.

The reset cases interrupt FTP and FWU lite after 1 or 80 erased pages, then
after 512 or 65,536 written bytes. Each case checks that the confirmed image
still boots and reads back unchanged. Resets happen between flash operations;
this does not test loss of power during an individual erase/program pulse.
Golden storage and the primary slot are never selected by the probes.

Add `flight-diagnostics.conf` to the flight configuration and pass `--stack-check`
to record stack high-water marks after the candidate's PARAM requests.

The Robot case in `tests/hil/fwu.robot` uses `KFSW_FWU_CAN_GROUND`,
`KFSW_FWU_CAN_IMAGE`, `KFSW_FWU_CAN_REVISION`, `KFSW_FWU_CAN_OUTPUT`, and
`KFSW_DEBUG_SERIAL`. It skips when image paths are unset.

## Adapter recovery

If bringing up a PCAN-USB fails with `Broken pipe` and the kernel reports a USB
command error `-32`, reset that adapter, then repeat the setup command:

```bash
sudo usbreset "PCAN-USB"
sudo bash k-fsw/tests/hil/stm32/nucleo-l496zg/can-up.sh 500000 normal
```

Check `ip -details -statistics link show can0` before transmitting.
`ERROR-ACTIVE` is the normal CAN controller state; record the error counters too.
