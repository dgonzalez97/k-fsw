# Holybro SiK 433 MHz HIL fixture

This fixture keeps raw-link and CSP/KISS acceptance separate for a Holybro /
SiK 433 MHz pair. The USB-side radio reports RFD SiK 2.0 on HM-TRP. Its
observed settings are:

| Setting | Value |
| --- | --- |
| `SERIAL_SPEED` | `57` (57600 baud) |
| `AIR_SPEED` | `64` |
| `NETID` | `25` |
| `TXPOWER` | `20` |
| `ECC` | `0` |
| `MAVLINK` | `1` |
| Frequency range | 433050–434790 kHz |
| Channels | `10` |
| `RTSCTS` | `0` |

Create a host-specific bench environment from the template, edit both stable
device paths, and source it from the workspace root. Create the mission
`ground-station/` first with `./k-fsw/tools/k-ground init` if it does not yet
exist:

```bash
cp ./k-fsw/tests/hil/radio-uhf/holybro/bench.env.example \
  ./ground-station/holybro-bench.env
${EDITOR:-vi} ./ground-station/holybro-bench.env
. ./ground-station/holybro-bench.env
```

The template uses `export`, so its variables are inherited by the HIL scripts.
Keep the actual bench file outside the reusable `k-fsw` repository. Prefer
stable `/dev/serial/by-id/...` paths over `/dev/ttyUSB0` or `/dev/ttyACM0`,
whose numbering can change after reconnecting hardware. Verify the selected
devices without writing to them:

```bash
printf 'radio=%s\ndebug=%s\nbaud=%s\n' \
  "$KGROUND_HOLYBRO_DEVICE" "$KFSW_DEBUG_SERIAL" "$KGROUND_HOLYBRO_BAUD"
readlink -f "$KGROUND_HOLYBRO_DEVICE"
readlink -f "$KFSW_DEBUG_SERIAL"
```

## Manual developer bring-up

The automated runners below are the acceptance authority, but a new developer
can also operate each layer manually. Begin at the west workspace root:

```bash
cd /path/to/K-FSW
. .venv/bin/activate
west update
west manifest --validate
west list kfsw-modules -f '{name} {revision} {sha}'
ls -l /dev/serial/by-id/
. ./ground-station/holybro-bench.env
```

`west update` must select the `kfsw-modules` revision pinned by `west.yml`.
Never use `/dev/ttyUSB0` or `/dev/ttyACM0` as the saved bench identity. Confirm
the two stable paths again after every WSL/usbipd reattachment.

Build and optionally run the normal Linux reference composition first:

```bash
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

Exit the Linux shell, then build the NUCLEO UHF composition. This composition
enables the reusable `radio-uhf`/Holybro module, selects node 2 with peer 16,
and applies the 57600-baud USART3 overlay without changing the radio itself:

```bash
export KFSW_UHF_NUCLEO_BUILD="$PWD/build/manual/holybro-nucleo"
KFSW_BUILD_DIR="$KFSW_UHF_NUCLEO_BUILD" \
KFSW_EXTRA_CONF_FILE="$PWD/k-fsw/tests/hil/radio-uhf/holybro/nucleo_l496zg.conf" \
KFSW_EXTRA_DTC_OVERLAY_FILE="$PWD/k-fsw/tests/hil/radio-uhf/holybro/nucleo_l496zg.overlay" \
KFSW_PRISTINE=always \
  ./k-fsw/tools/build.sh nucleo_l496zg

KFSW_BUILD_DIR="$KFSW_UHF_NUCLEO_BUILD" \
  ./k-fsw/tools/flash.sh nucleo_l496zg
```

Open the ST-LINK debug shell in one terminal. `tools/serial.sh` is a bounded
capture tool, not an interactive terminal, so manual command entry currently
uses the host `picocom` package:

```bash
picocom -b 115200 --flow n "$KFSW_DEBUG_SERIAL"
```

In a second terminal, activate/source the environment again and launch the UHF
ground role. The role configuration selects Holybro; `--peer 2` selects the
flight endpoint, and the physical overlay makes the native KISS PTY 57600:

```bash
cd /path/to/K-FSW
. .venv/bin/activate
. ./ground-station/holybro-bench.env
export KGROUND_BUILD_ROOT="$PWD/build/manual/holybro-ground"
KFSW_EXTRA_DTC_OVERLAY_FILE="$PWD/k-fsw/tests/hil/radio-uhf/holybro/k-ground.overlay" \
  ./k-fsw/tools/k-ground run kfsw-gnd-uhf --peer 2 --no-local-link
```

Copy the `/dev/pts/N` path printed as `uart_1 connected to pseudotty:`. In a
third terminal, bridge that exact PTY to the stable ground-radio device:

```bash
cd /path/to/K-FSW
. ./ground-station/holybro-bench.env
socat -d -d \
  /dev/pts/N,raw,echo=0,b57600 \
  "$KGROUND_HOLYBRO_DEVICE",raw,echo=0,b57600
```

The second terminal is the node-16 shell. Inspect composition, radio facts,
CSP, routes, and counters before traffic:

```text
kfsw-gnd-uhf# status
kfsw-gnd-uhf# uhf status
kfsw-gnd-uhf# csp info
kfsw-gnd-uhf# csp interfaces
kfsw-gnd-uhf# csp routes
kfsw-gnd-uhf# uart info
kfsw-gnd-uhf# csp ping 2
```

The NUCLEO `picocom` terminal is the node-2 shell:

```text
kfsw:~$ status
kfsw:~$ uhf status
kfsw:~$ csp info
kfsw:~$ csp interfaces
kfsw:~$ csp routes
kfsw:~$ uart info
kfsw:~$ csp ping 16
```

`uhf status` must identify `holybro-sik`, show expected serial `57600 8N1`,
and leave RF link `unknown`; it does not query the modem. `uart info` and
`csp interfaces` report the actual transport and counters.

Use real production parameters to prove a service across RF. Record the
initial value, select a different non-default value in `0..4`, and read it
back. While that value is still active, send an invalid value and verify that
the target restores the compiled default. The current composition compiles
`log_level` with default `1`. Do not run `param save` during this trial:

```text
kfsw-gnd-uhf# param list 2
kfsw-gnd-uhf# param get 2 node_id
kfsw-gnd-uhf# param get 2 log_level
kfsw-gnd-uhf# param set 2 log_level 3
kfsw-gnd-uhf# param get 2 log_level
kfsw-gnd-uhf# param set 2 log_level 5
kfsw-gnd-uhf# param get 2 log_level
```

The production list contains `node_id` and `log_level`; it must not contain
`test_u32`, `test_i32`, or `test_float`. Finish with bounded negative checks
and confirm both shells remain responsive:

```text
kfsw-gnd-uhf# param get 2 missing
kfsw-gnd-uhf# csp ping 3
kfsw-gnd-uhf# status
kfsw-gnd-uhf# uart info
kfsw-gnd-uhf# csp interfaces
```

The invalid external value must be rejected and `log_level` must return from
the verified non-default value to its compiled default `1`; retaining the
previous non-default value is a failure. The missing name must be rejected,
and node 3 must time out cleanly. Stop the bridge and both shells with `Ctrl-C`
when finished.

Current DX friction is explicit: the interactive console needs external
`picocom`; manual ground-to-radio operation requires copying a generated PTY
into a separate `socat` command; and the local node-16/node-19 software demo
uses its native 115200-baud PTY even though `uhf status` records the deployed
Holybro expectation of 57600. The physical overlay aligns the actual UART and
expected radio rate. These are candidates for a later developer-tooling issue,
not reasons to add another transport abstraction.

## Raw bytes first

`raw-nucleo-smoke.sh` builds and flashes the temporary `raw-peer/` application,
waits for its ST-LINK readiness marker, then uses `raw-smoke.sh` to exchange:

```text
KGROUND-RAW-PING 0001
KGROUND-RAW-PONG 0001
```

This is HIL-only test traffic, not a K-FSW protocol or production radio module.
Run one exchange first, then the 100-exchange acceptance. `--count` increments
the four-digit starting sequence and reports completed exchanges, invalid
payloads, and timeouts:

```bash
./k-fsw/tests/hil/radio-uhf/holybro/raw-nucleo-smoke.sh --count 1
./k-fsw/tests/hil/radio-uhf/holybro/raw-nucleo-smoke.sh --count 100
```

The temporary raw peer continuously polls USART3. At 57600 baud, sleeping or
printing after reception starts can let a burst overrun this deliberately
simple polling fixture. Production CSP/KISS reception is interrupt-driven and
does not use this raw-peer loop.

## CSP/KISS second

`csp-kiss-smoke.sh` builds `kfsw-gnd-uhf` node 16 and NUCLEO node 2 with a
57600-baud KISS UART, flashes the NUCLEO, bridges the ground PTY to the USB
radio, and verifies both interfaces, the direct KISS routes, bidirectional CSP
ping, production PARAM list/get/set with owner validation/callback behavior,
bounded negative cases, and clean nonzero post-traffic counters:

```bash
./k-fsw/tests/hil/radio-uhf/holybro/csp-kiss-smoke.sh
```

The electrical fixture must use the board/radio-compatible logic level, a
common ground, crossed TX/RX, and no hardware flow control. Confirm the actual
radio and board pinouts before applying power.

## Evidence classification

### PHYSICALLY BENCH VERIFIED

The physical bench run on 30 August 2026 verified raw UART/RF, bidirectional
CSP/KISS, and remote PARAM transport over Holybro. The stable WSL devices were:

| Function | USB identity | Stable device |
| --- | --- | --- |
| Ground Holybro FTDI | `0403:6015`, FT230X serial `DN05YTP0` | `/dev/serial/by-id/usb-FTDI_FT230X_Basic_UART_DN05YTP0-if00-port0` |
| NUCLEO ST-LINK console/debug | `0483:374b`, ST-LINK serial `0670FF3732504E3043093407` | `/dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_0670FF3732504E3043093407-if02` |

The user observed established RF link LEDs after separating the bench
power/USB arrangement and had independently measured 100/100 direct USB radio
exchanges in each direction. No persistent SiK parameter was changed.

K-FSW acceptance produced this physical evidence:

- the basic raw request/reply passed through USART3 PD8/PD9;
- sequences `0001` through `0100` passed 100/100, with zero invalid payloads
  and zero timeouts, and the NUCLEO logged all 100 exchanges;
- NUCLEO node 2 and ground node 16 each exposed `KISS addr=<node>/0` and
  `0/0 -> KISS direct` at 57600 baud;
- node 16 pinged node 2 in 230 ms, and node 2 pinged node 16 in 172 ms;
- the production remote PARAM list contained only `node_id` and `log_level`;
  `log_level` changed from 1 to 3, its owner callback reduced `log test` to
  ERROR-only output, and the fixture then restored `log_level` to 1;
- after that restoration, invalid `log_level=5` ended with `log_level=1`; this
  historical sequence did not distinguish retaining the current value from
  resetting to the compiled default; a missing parameter and node 3 were
  rejected cleanly, and the shell remained responsive; and
- both endpoints ended with KISS `tx=14 rx=14`, with `txerr=0`, `rxerr=0`,
  `drop=0`, and UART `frame=0`.

### SOFTWARE VERIFIED

The corrected discriminating PARAM oracle is verified in software:

```text
initial/compiled default = 1
valid set                = 3
invalid request          = 5
final value              = 1
```

This proves the current production behavior: invalid externally supplied
values are rejected and the parameter is restored to its compiled default.

### PENDING PHYSICAL RE-RUN

The physical HIL script now leaves the verified non-default value `3` active,
sends invalid value `5`, and requires the final value to be the compiled
default `1`. That corrected `1 -> 3 -> invalid 5 -> 1` oracle has not yet been
rerun on the physical Holybro bench.

The initial raw rerun exposed a separate defect in the HIL-only polling peer:
diagnostic output and a one-millisecond idle sleep could stall or overrun a
57600-baud burst. Removing those operations was the only functional source
change required. The normal K-FSW UART/KISS receive path is interrupt-driven,
and its successful bidirectional CSP result does not indicate a production
UART/RF software defect. Together with the changed power/USB arrangement, the
new evidence supports attributing the earlier end-to-end CSP failure to bench
conditions, while the earlier raw result was also affected by the test-peer
defect.

This is verified functional evidence for this Holybro SiK raw UART/RF and
CSP/KISS bench. It is not RF qualification, production readiness, flight
qualification, an RF performance measurement, or a long-duration link test.

`kfsw-modules` now owns the reusable `radio-uhf` interface and its first
`holybro-sik` implementation. The module owns compile-time identity, expected
serial configuration, and bounded status. It deliberately does not own UART,
KISS, CSP, AT control, or any writable radio parameter; those omissions keep
the verified transparent data path unchanged.
