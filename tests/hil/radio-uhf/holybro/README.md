# Holybro SiK 433 MHz HIL fixture

This fixture prepares, but does not claim, physical verification of a Holybro /
SiK 433 MHz link. The current USB-side radio reports RFD SiK 2.0 on HM-TRP.
The recorded bench settings are:

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

`raw-smoke.sh` sends one deterministic line and requires a separately prepared
peer to return the matching response:

```text
KGROUND-RAW-PING 0001
KGROUND-RAW-PONG 0001
```

This is only a test exchange; it is not a K-FSW protocol. The peer may be a
temporary serial echo fixture. Run it only when that peer and both radio ends
are ready:

```bash
KGROUND_HOLYBRO_DEVICE=/dev/serial/by-id/<usb-radio> \
  ./k-fsw/tests/hil/radio-uhf/holybro/raw-smoke.sh
```

## CSP/KISS second

`csp-kiss-smoke.sh` builds k-ground node 16 and NUCLEO node 2 with a 57600-baud
KISS UART, flashes the NUCLEO, bridges the ground PTY to the USB radio, and
requires `csp ping 2` to succeed. The NUCLEO-side Holybro must be connected to
the selected CSP UART before running it:

```bash
KGROUND_HOLYBRO_DEVICE=/dev/serial/by-id/<usb-radio> \
KFSW_DEBUG_SERIAL=/dev/serial/by-id/<st-link-vcp> \
  ./k-fsw/tests/hil/radio-uhf/holybro/csp-kiss-smoke.sh
```

The electrical fixture must use the board/radio-compatible logic level, a
common ground, crossed TX/RX, and no hardware flow control. Confirm the actual
radio and board pinouts before applying power.

The two tests intentionally remain separate: raw success proves bytes traverse
UART/RF, while CSP success additionally proves KISS framing, routing, and the
CSP endpoints. Both are pending until the complete radio bench is present.

The dormant `kfsw-modules` repository is not currently a west project and has
no committed module baseline. Reusable Holybro control or radio-management code
belongs under `radio-uhf/holybro` there once that repository is initialized and
added to the manifest. This prototype adds only K-FSW-owned HIL fixtures and
does not invent a radio driver.
