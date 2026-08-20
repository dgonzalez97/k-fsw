# K Flight Software — K-FSW

K-FSW is an open-source flight software framework for satellites, built
around Zephyr RTOS.

This composition repository owns the application, west manifest, board
configuration, developer tools, test support, CI.

Supported targets:

- `nucleo_l496zg`: STM32 NUCLEO-L496ZG
- `linux`: Zephyr `native_sim/native/64`

Both targets run the same K-FSW application and services on Zephyr RTOS
4.4.0.

Build and run KFSW-Linux:

```bash
./k-fsw/tools/build.sh linux
./k-fsw/tools/run-linux.sh
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
