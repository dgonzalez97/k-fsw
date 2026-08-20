# K Flight Software — K-FSW

K-FSW is an open-source flight software framework for satellites, built
around Zephyr RTOS.

This composition repository owns the application, west manifest, board
configuration, developer tools, test support, CI.

Supported profiles:

- `nucleo_l496zg`: STM32 NUCLEO-L496ZG
- `nucleo_l496zg_uart`: NUCLEO with CSP KISS on USART3 PD8/PD9
- `linux`: Zephyr `native_sim/native/64`
- `linux_uart`: Linux CSP node 1 for a physical UART bridge

Both targets run the same K-FSW application and services on Zephyr RTOS
4.4.0.

Build and run KFSW-Linux:

```bash
./k-fsw/tools/build.sh linux
./k-fsw/tools/run-linux.sh
```

## Dedicated CSP UART

The UART profile keeps the Zephyr shell on the NUCLEO ST-LINK VCP at
`/dev/ttyACM0` (LPUART1 PG7/PG8). CSP uses a separate 115200 8N1 KISS link on
USART3: D1/PD8 is TX and D0/PD9 is RX.

Configure the YP-05 FT232RL for **3.3 V logic** and connect only:

- NUCLEO D1 / PD8 / USART3_TX to YP-05 RX
- NUCLEO D0 / PD9 / USART3_RX to YP-05 TX
- NUCLEO GND to YP-05 GND

Do not connect VCC, DTR, or CTS. Keep the NUCLEO powered through ST-LINK and
the YP-05 powered through its own USB cable. Prefer its stable
`/dev/serial/by-id/...` path, then run:

```bash
./k-fsw/tools/uart-csp-smoke.sh --ftdi /dev/serial/by-id/<FT232RL>
```

The tool builds and flashes the UART profile, discovers the KFSW-Linux KISS
PTY, bridges it to the FTDI device with `socat`, exercises CSP in both
directions, runs the permanent UART transport test, checks KISS statistics,
and cleans up all child processes.

## CSP development link

The Linux image enables libcsp node 1 by default. A second build profile sets
node address 2 without duplicating the application:

```bash
./k-fsw/tools/build.sh linux
./k-fsw/tools/build.sh linux_node2
```

Launch each node in a separate terminal:

```bash
./k-fsw/tools/run-linux.sh --node 1 --device_id=1
./k-fsw/tools/run-linux.sh --node 2 --device_id=2
```

Each process prints a PTY for `uart_1`. Connect those two paths with libcsp's
USART/KISS byte link (replace the example PTYs with the printed values):

```bash
socat /dev/pts/7,raw,echo=0 /dev/pts/9,raw,echo=0
```

Then use the shell console on node 1 to run `kfsw csp ping 2`. The deterministic
integration test builds missing profiles, discovers and bridges the CSP PTYs,
checks the CSP shell commands, pings in both directions, and cleans up every
process:

```bash
./k-fsw/tools/csp-smoke.sh
```

## Debug shell

The development configuration enables a small Zephyr serial shell rooted at
`kfsw`. To use it on KFSW-Linux, start the simulator and note the PTY printed
on its first line:

```bash
./k-fsw/tools/run-linux.sh
# uart connected to pseudotty: /dev/pts/7
```

In another terminal, connect to the reported path:

```bash
picocom /dev/pts/7
```

Exit picocom with `Ctrl-A`, then `Ctrl-X`, and stop the simulator with
`Ctrl-C`. For a non-interactive check of the initial command set, run:

```bash
./k-fsw/tools/shell-smoke.sh
```

On the NUCLEO-L496ZG, connect to the existing console UART after flashing:

```bash
picocom --baud 115200 /dev/ttyACM0
```

The shell shares that UART with boot markers and K-FSW log output. Exit
picocom with `Ctrl-A`, then `Ctrl-X`.
