# Holybro SiK 433 MHz radio tests

## Encrypted link

Add `config/profiles/radio-crypto.conf` on both ends. The flight build also
needs `nucleo-radio-crypto.overlay` and `crypto-flight.conf`, and the ground
build `crypto-ground.conf`. This bench uses flight node 2, radio ground node 17
and CAN ground node 16, with the UART at 57600 baud.

Create a key file, then run the test against a confirmed flight image:

```bash
umask 077
openssl rand -hex 32 > /tmp/radio-bench.key
python k-fsw/tests/hil/radio-uhf/holybro/crypto-smoke.py \
  --ground "$GROUND_BUILD/zephyr/zephyr.exe" \
  --serial "$KFSW_DEBUG_SERIAL" --radio "$KFSW_RADIO_SERIAL" \
  --key-file /tmp/radio-bench.key --output "$GROUND_BUILD/crypto-run"
```

Both serial paths must be `/dev/serial/by-id/` paths. The test sets the key,
checks both directions, checks that remote writes to radio settings are
refused, replays captured traffic, tries a wrong key and plaintext, and reboots
the flight node. The key stays configured afterwards. Logs hide the key, but
keep the key file private.

To run it without hardware, replace `--serial` and `--radio` with `--flight`
and a native flight executable configured as node 2, peer 17, with the prompt
`crypto-flight# `.

## Radio settings

The USB radio reports RFD SiK 2.0 on HM-TRP with these settings:

| Setting | Value |
| --- | --- |
| `SERIAL_SPEED` | `57` (57600 baud) |
| `AIR_SPEED` | `64` |
| `NETID` | `25` |
| `TXPOWER` | `20` |
| `ECC` | `0` |
| `MAVLINK` | `1` |
| Frequency range | 433050-434790 kHz |
| Channels | `10` |
| `RTSCTS` | `0` |

Create a bench file for your host from the template, set both device paths and
source it from the workspace root. Run `./k-fsw/tools/k-ground init` first if
`ground-station/` doesn't exist yet:

```bash
cp ./k-fsw/tests/hil/radio-uhf/holybro/bench.env.example \
  ./ground-station/holybro-bench.env
${EDITOR:-vi} ./ground-station/holybro-bench.env
. ./ground-station/holybro-bench.env
```

The template uses `export`, so the test scripts see the variables. Keep the
bench file out of the `k-fsw` repository, and use `/dev/serial/by-id/...` paths,
since `/dev/ttyUSB0` and `/dev/ttyACM0` can change after reconnecting. Check
the devices without writing to them:

```bash
printf 'radio=%s\ndebug=%s\nbaud=%s\n' \
  "$KGROUND_HOLYBRO_DEVICE" "$KFSW_DEBUG_SERIAL" "$KGROUND_HOLYBRO_BAUD"
readlink -f "$KGROUND_HOLYBRO_DEVICE"
readlink -f "$KFSW_DEBUG_SERIAL"
```

## Manual setup

The scripts below do all of this, but each step can also be run by hand. From
the workspace root:

```bash
cd /path/to/K-FSW
. .venv/bin/activate
west update
west manifest --validate
west list kfsw-modules -f '{name} {revision} {sha}'
ls -l /dev/serial/by-id/
. ./ground-station/holybro-bench.env
```

Check the device paths again after every WSL/usbipd reattach.

Build and flash the NUCLEO radio image. It enables the `radio-uhf` module, uses
node 2 with peer 16 and sets USART3 to 57600 baud:

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

Open the ST-LINK console in one terminal. `tools/serial.sh` only captures
output, so use `picocom` to type commands:

```bash
picocom -b 115200 --flow n "$KFSW_DEBUG_SERIAL"
```

In a second terminal, start the UHF ground node. `--peer 2` selects the flight
node, and the overlay sets the native KISS PTY to 57600:

```bash
cd /path/to/K-FSW
. .venv/bin/activate
. ./ground-station/holybro-bench.env
export KGROUND_BUILD_ROOT="$PWD/build/manual/holybro-ground"
KFSW_EXTRA_DTC_OVERLAY_FILE="$PWD/k-fsw/tests/hil/radio-uhf/holybro/k-ground.overlay" \
  ./k-fsw/tools/k-ground run kfsw-gnd-uhf --peer 2 --no-local-link
```

Copy the `/dev/pts/N` path printed after `uart_1 connected to pseudotty:`. In a
third terminal, connect that PTY to the ground radio:

```bash
cd /path/to/K-FSW
. ./ground-station/holybro-bench.env
socat -d -d \
  /dev/pts/N,raw,echo=0,b57600 \
  "$KGROUND_HOLYBRO_DEVICE",raw,echo=0,b57600
```

On the node 16 shell:

```text
kfsw-gnd-uhf# status
kfsw-gnd-uhf# uhf status
kfsw-gnd-uhf# csp info
kfsw-gnd-uhf# csp interfaces
kfsw-gnd-uhf# csp routes
kfsw-gnd-uhf# uart info
kfsw-gnd-uhf# csp ping 2
```

On the NUCLEO console, node 2:

```text
kfsw:~$ status
kfsw:~$ uhf status
kfsw:~$ csp info
kfsw:~$ csp interfaces
kfsw:~$ csp routes
kfsw:~$ uart info
kfsw:~$ csp ping 16
```

`uhf status` should show `holybro-sik`, expected serial `57600 8N1` and RF link
`unknown`. `uart info` and `csp interfaces` show the traffic and counters.

Then try a parameter over the radio. Set `log_level` to another value between
0 and 4 and read it back, then send an invalid value; the node should go back
to its default of `1`. Don't run `param save` during this test.

```text
kfsw-gnd-uhf# param list 2
kfsw-gnd-uhf# param get 2 node_id
kfsw-gnd-uhf# param get 2 log_level
kfsw-gnd-uhf# param set 2 log_level 3
kfsw-gnd-uhf# param get 2 log_level
kfsw-gnd-uhf# param set 2 log_level 5
kfsw-gnd-uhf# param get 2 log_level
```

The list shouldn't include the `test_*` parameters. Finish with a few error
cases and check that both shells still respond:

```text
kfsw-gnd-uhf# param get 2 missing
kfsw-gnd-uhf# csp ping 3
kfsw-gnd-uhf# status
kfsw-gnd-uhf# uart info
kfsw-gnd-uhf# csp interfaces
```

The missing name should be rejected and the ping to node 3 should time out.
Stop the bridge and both shells with `Ctrl-C`.

The local node 16 and node 19 demo uses a 115200-baud PTY even though
`uhf status` shows the radio's 57600; the radio overlay sets both to 57600.

## Raw bytes

`raw-nucleo-smoke.sh` builds and flashes the `raw-peer/` test application,
waits for it on the ST-LINK console and uses `raw-smoke.sh` to exchange:

```text
KGROUND-RAW-PING 0001
KGROUND-RAW-PONG 0001
```

This is only test traffic, not a K-FSW protocol. Run one exchange, then 100.
`--count` sets the number of exchanges, and the script reports the completed
exchanges, bad payloads and timeouts:

```bash
./k-fsw/tests/hil/radio-uhf/holybro/raw-nucleo-smoke.sh --count 1
./k-fsw/tests/hil/radio-uhf/holybro/raw-nucleo-smoke.sh --count 100
```

The raw peer polls USART3, so at 57600 baud printing or sleeping while bytes
arrive can overrun it. The K-FSW UART receive path is interrupt driven and
doesn't have this problem.

## CSP/KISS

`csp-kiss-smoke.sh` builds ground node 16 and NUCLEO node 2 with a 57600-baud
KISS UART, flashes the NUCLEO, connects the ground PTY to the USB radio, and
checks the interfaces, routes, ping in both directions, parameters, error cases
and counters:

```bash
./k-fsw/tests/hil/radio-uhf/holybro/csp-kiss-smoke.sh
```

Use the right logic level for the board and radio, a common ground, crossed
TX/RX and no hardware flow control. Check the pinouts before powering up.

## Beacons

`beacon-smoke.sh` checks housekeeping beacons over the radio. The ground side is
`hk-bridge.py --listen`, which sends nothing, so every frame it gets was sent by
the node on its own.

```bash
./k-fsw/tests/hil/radio-uhf/holybro/beacon-smoke.sh
```

It also checks that an interval below the minimum is refused, that the counters
match what the ground received, and that beacons can be turned off.

`sent` can be a little higher than `heard`: beacons don't use RDP, so a lost
frame is a lost sample, and the sequence numbers show which one.

## Results

On 30 August 2026 this bench passed the raw test and the CSP/KISS test,
including remote parameters. The devices were:

| Function | USB identity | Device |
| --- | --- | --- |
| Ground Holybro FTDI | `0403:6015`, FT230X serial `DN05YTP0` | `/dev/serial/by-id/usb-FTDI_FT230X_Basic_UART_DN05YTP0-if00-port0` |
| NUCLEO ST-LINK | `0483:374b`, serial `0670FF3732504E3043093407` | `/dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_0670FF3732504E3043093407-if02` |

- The raw exchange passed 100 out of 100, with no bad payloads or timeouts.
- Both nodes showed `KISS addr=<node>/0` and `0/0 -> KISS direct` at 57600 baud.
- Node 16 pinged node 2 in 230 ms, and node 2 pinged node 16 in 172 ms.
- `log_level` was changed from 1 to 3 over the radio and set back to 1.
- Both ends ended with KISS `tx=14 rx=14` and no errors or drops.

The invalid value check (1, 3, then 5, back to 1) was added after that run and
has only run in software so far. No SiK settings were changed.
