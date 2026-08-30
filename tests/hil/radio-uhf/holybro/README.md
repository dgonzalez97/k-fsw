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
ping, and clean nonzero post-traffic counters:

```bash
./k-fsw/tests/hil/radio-uhf/holybro/csp-kiss-smoke.sh
```

The electrical fixture must use the board/radio-compatible logic level, a
common ground, crossed TX/RX, and no hardware flow control. Confirm the actual
radio and board pinouts before applying power.

## Verified bench result

The corrected physical bench passed on 30 August 2026. The stable WSL devices
were:

| Function | USB identity | Stable device |
| --- | --- | --- |
| Ground Holybro FTDI | `0403:6015`, FT230X serial `DN05YTP0` | `/dev/serial/by-id/usb-FTDI_FT230X_Basic_UART_DN05YTP0-if00-port0` |
| NUCLEO ST-LINK console/debug | `0483:374b`, ST-LINK serial `0670FF3732504E3043093407` | `/dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_0670FF3732504E3043093407-if02` |

The user observed established RF link LEDs after separating the bench
power/USB arrangement and had independently measured 100/100 direct USB radio
exchanges in each direction. No persistent SiK parameter was changed.

K-FSW acceptance then produced this evidence:

- the basic raw request/reply passed through USART3 PD8/PD9;
- sequences `0001` through `0100` passed 100/100, with zero invalid payloads
  and zero timeouts, and the NUCLEO logged all 100 exchanges;
- NUCLEO node 2 and ground node 16 each exposed `KISS addr=<node>/0` and
  `0/0 -> KISS direct` at 57600 baud;
- node 16 pinged node 2 in 180 ms, and node 2 pinged node 16 in 175 ms; and
- both endpoints ended with KISS `tx=2 rx=2`, with `txerr=0`, `rxerr=0`,
  `drop=0`, and UART `frame=0`.

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

The dormant `kfsw-modules` repository is not currently a west project and has
no committed module baseline. Reusable Holybro control or radio-management code
belongs under `radio-uhf/holybro` there once that repository is initialized and
added to the manifest. This prototype adds only K-FSW-owned HIL fixtures and
does not invent a radio driver.
