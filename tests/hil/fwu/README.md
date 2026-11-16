# Firmware update over CAN

NUCLEO-L496ZG node 2, ground node 17, 500 kbit/s. Connect and terminate the
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

## Reference-image load

`can-acceptance.py` uses the existing CAN ground node and an explicitly selected
minimal or OBC baseline. It does not build or flash the baseline. Verify its
installation independently before running. Use the reference-image recipes in
[the requirements guide](../../requirements/README.md) and retain each image's
resolved configuration and independently recorded build context.

The ground executable needs its adjacent `.config`, address 17, CSP2, CAN at
500,000 bit/s and `0/0 CAN`. The fixture verifies those inputs before opening a
serial device or launching the executable. It copies the executable and ground
filesystem snapshot into a new output directory and launches that copy. The
original ground filesystem remains unchanged. The target is flight node 2;
its route to 17 must use CAN. Native `--device_id=17` does not change the
compiled CSP address, which is also checked through runtime interface output.

For a short minimal smoke run, set the paths to the actual controlled build:

```bash
python k-fsw/tests/hil/fwu/can-acceptance.py \
  --ground "$B/ground/zephyr/zephyr.exe" \
  --flash "$KFSW_FWU_CAN_FLASH" \
  --flight-image "$KFSW_REFERENCE_IMAGE" \
  --flight-config "$KFSW_REFERENCE_CONFIG" \
  --context "$KFSW_REFERENCE_CONTEXT" \
  --profile minimal --seconds 60 \
  --serial "$KFSW_DEBUG_SERIAL" \
  --output "$B/minimal-load-1"
```

`KFSW_FWU_CAN_FLASH` names an existing ground flash file with the CAN fixture's
2 MiB filesystem. Preparing this ground file does not require running the
firmware-update campaign. `KFSW_REFERENCE_IMAGE` is the selected baseline BIN,
`KFSW_REFERENCE_CONFIG` its resolved `.config`, and `KFSW_REFERENCE_CONTEXT` the
version 1 context described in [image binding](../../requirements/image-binding.md).
The context must name `minimal-l496`, physical evidence and the actual config
SHA-256. The fixture checks the resolved reference requirements and records
input hashes, the supplied context, fixture-source hashes and pre/post target
revision strings. These consistency checks do not attest installed bytes or
prove the named sources produced the binary. Controlled build, installation and
readback records establish that connection separately.

The minimal load uses built-in `node_id`, read-only `status 2`, verified 4,096-byte
FTP file round trips and primary-slot downloads. It issues no HK, temperature,
UTC-setting or timing-probe commands. It changes the target test file
`/soak.bin`; preserve and restore filesystem state through the bench campaign.

For the OBC image, use `--profile obc` and the matching `obc-l496` build/context.
This adds local and remote HK at 1 Hz using board/storage parameters and node
17, plus a beacon every 5 seconds. It verifies advancing collection counters and
rejects new collection failures or absent values. When measured active time reaches
the 5-second beacon interval, the beacon count must also advance. This includes
a short requested run extended by an in-flight transfer. There is no dependency
on the temperature example. The fixture records HK settings before and after, then
stops the periods and beacon. It does not restore overwritten report definitions
or their persisted state; back up and restore the bench state separately.

A ground console serialises commands. PARAM and status latency is therefore
measured between completed transfers, not during concurrent FTP. Measurements
use host monotonic time around each command and include ground console,
scheduling, link and target work. Full OBC HK runs concurrently on the target.
A concurrent minimal PARAM/FTP test needs a separately declared client/link and
remains outside this fixture's current claim.

`soak.json` records requested and active duration, overshoot, attempts, successes,
failures, verified cycles, PARAM/control counts and observed maxima.
`verified_roundtrip_bytes` counts verified file contents, 4,096 bytes per successful
comparison; it does not count both directions or protocol overhead. Per-operation
durations and commands are in `operations.jsonl`. `run.json` records artifact
inputs, target revisions and overall status. Console logs and CAN state preserve
raw observations. A first failure remains recorded if cleanup also fails.

After the 60-second smoke succeeds, use `--seconds 3600` and a fresh output
directory for the sustained run. A started transfer can finish after the requested
duration; the result records the actual duration. Zero seconds without an
interruption campaign, or a run with no completed transfer/PARAM/control work,
fails. Maxima are observed samples, not a WCET proof or a mission deadline bound.
No physical load result is implied by these commands or the offline runner tests.

## Optional timing probes

For an explicitly instrumented OBC bench image, put `probes.conf` after the
reference fragment and pass `--probes`. Its context profile is
`obc-l496-probes`. The runner derives that selection from OBC requirements with
only the probe expectation changed; this is a distinct instrumented selection,
not a pass of the unmodified reference-profile checker or evidence for an
uninstrumented binary. The fixture never enables a firmware option itself.
Minimal load rejects probes.

The probes measure wake-to-run latency, HK collection time, local PARAM reads
and PARAM sampling. `timing.txt` records observed maxima; no measured HK
collections or nonzero untracked threads fail the probe check. Stack observations
are saved separately. Both the actual image and configuration must identify the
instrumented build. Host probe tests do not establish Cortex-M timing.

## Interrupted uploads

Interruption testing remains a separate operation on a confirmed, probes-enabled
baseline. The existing zero-duration invocation is valid for this operation:

```bash
python k-fsw/tests/hil/fwu/can-acceptance.py \
  --ground "$B/ground/zephyr/zephyr.exe" \
  --image "$B/after/app/zephyr/zephyr.signed.bin" \
  --flash "$B/run-1/ground-flash.bin" \
  --serial "$KFSW_DEBUG_SERIAL" \
  --seconds 0 --interruptions --output "$B/interruptions-1"
```

Here `--image` is the candidate for the update operation. It is not the running
baseline artifact. The legacy interruption-only invocation does not bind a
baseline build context and cannot satisfy reference-load evidence. Combining
positive-duration load and interruptions requires an explicit OBC probe selection.
Keep baseline, candidate, trial/revert/confirmed and restored identities in the
enclosing campaign rather than bind all phases to one image hash.

The reset cases interrupt FTP and FWU lite after 1 or 80 erased pages, then
after 512 or 65,536 written bytes. Each case checks that the confirmed image
still boots and reads back unchanged. Resets happen between flash operations;
this does not test loss of power during an individual erase/program pulse.
Golden storage and the primary slot are never selected by the probes.

The update script leaves its confirmed candidate running. Neither script restores
the original complete target flash automatically. Preserve the actual starting
flash and verify restoration separately, including after failures.

Add `flight-diagnostics.conf` to the flight configuration and pass `--stack-check`
to `can-update.py` to record stack high-water marks after candidate PARAM requests.

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
