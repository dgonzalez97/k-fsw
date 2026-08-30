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

Copy `bench.env.example` outside the repository or export the same variables
directly. Prefer stable `/dev/serial/by-id/...` paths.

## Raw bytes first

`raw-nucleo-smoke.sh` builds and flashes the temporary `raw-peer/` application,
waits for its ST-LINK readiness marker, then uses `raw-smoke.sh` to exchange:

```text
KGROUND-RAW-PING 0001
KGROUND-RAW-PONG 0001
```

This is HIL-only test traffic, not a K-FSW protocol or production radio module.
Run it with both serial paths explicit:

```bash
KGROUND_HOLYBRO_DEVICE=/dev/serial/by-id/<usb-radio> \
KFSW_DEBUG_SERIAL=/dev/serial/by-id/<st-link-vcp> \
  ./k-fsw/tests/hil/radio-uhf/holybro/raw-nucleo-smoke.sh
```

## CSP/KISS second

`csp-kiss-smoke.sh` builds `kfsw-gnd-uhf` node 16 and NUCLEO node 2 with a
57600-baud KISS UART, flashes the NUCLEO, bridges the ground PTY to the USB
radio, and requires `csp ping 2` to succeed:

```bash
KGROUND_HOLYBRO_DEVICE=/dev/serial/by-id/<usb-radio> \
KFSW_DEBUG_SERIAL=/dev/serial/by-id/<st-link-vcp> \
  ./k-fsw/tests/hil/radio-uhf/holybro/csp-kiss-smoke.sh
```

The electrical fixture must use the board/radio-compatible logic level, a
common ground, crossed TX/RX, and no hardware flow control. Confirm the actual
radio and board pinouts before applying power.

## Current bench result

The connected bench was exercised on 30 August 2026. Both applications built
and flashed, the NUCLEO debug console became ready, and both CSP endpoints
reported the expected node, peer, and 57600-baud configuration. Physical
acceptance did not pass:

- the raw client timed out waiting for `KGROUND-RAW-PONG 0001`;
- the NUCLEO raw peer reported no received USART3 byte;
- the local USB radio accepted command mode and reported the settings above,
  but remote `RTI` and `RTI5` queries received no answer; and
- the CSP/KISS bridge started correctly, but node 16 could not ping node 2.

This evidence places the current problem below CSP, in the remote-radio/RF or
NUCLEO-side UART path. Check RF pairing and both radios' settings, power/common
ground, voltage level, and crossed TX/RX before rerunning raw acceptance. Do
not treat LED activity alone as a passing data link.

The dormant `kfsw-modules` repository is not currently a west project and has
no committed module baseline. Reusable Holybro control or radio-management code
belongs under `radio-uhf/holybro` there once that repository is initialized and
added to the manifest. This prototype adds only K-FSW-owned HIL fixtures and
does not invent a radio driver.
