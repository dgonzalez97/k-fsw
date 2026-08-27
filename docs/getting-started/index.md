# Getting Started {#getting_started}

## Overview

K-FSW is a west workspace, not a single isolated checkout. `k-fsw` owns the
manifest and application composition; west checks out the exact platform,
services, communications, Zephyr, and upstream library revisions needed to
reproduce it.

The commands below assume the workspace root is the current directory and the
composition repository is at `./k-fsw`.

## Workspace setup

Install Git, Python 3, CMake, Ninja, west, and the Zephyr SDK appropriate for
Zephyr 4.4. Then create or update the workspace from the manifest:

```bash
west init -l k-fsw
west update
west zephyr-export
west manifest --validate
```

Use a Python virtual environment at the workspace root for developer tools.
The K-FSW scripts automatically activate `.venv` when it exists.

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install west robotframework==7.4.2
```

## Build

Build one profile with the project-owned wrapper:

```bash
./k-fsw/tools/build.sh linux
./k-fsw/tools/build.sh linux_uart
./k-fsw/tools/build.sh nucleo_l496zg
./k-fsw/tools/build.sh nucleo_l496zg_uart
```

Outputs are isolated under `build/<profile>/`. Set `KFSW_PRISTINE=always` to
force a clean Zephyr build, or use the CI entry point for the complete pristine
matrix:

```bash
./k-fsw/tools/ci/build.sh
```

## KFSW-Linux

KFSW-Linux is the normal K-FSW application built for Zephyr native simulation.
It uses the same platform, service, communications, and shell code as the
embedded profiles.

```bash
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

Startup completes with stable automation markers and the Zephyr shell prompt:

```text
@BOOT sw=kfsw-dev board=native_sim/native/64 ...
@READY uptime_ms=...

kfsw:~$
```

The `uart_1 connected to pseudotty:` line identifies the simulated CSP KISS
transport. It is not the interactive shell. Press `Ctrl-C` to stop the node.

## NUCLEO-L496ZG

The base and UART profiles target the NUCLEO-L496ZG:

```bash
./k-fsw/tools/build.sh nucleo_l496zg
./k-fsw/tools/build.sh nucleo_l496zg_uart
```

The UART profile adds CSP KISS on USART3 while the debug shell and logs remain
on the ST-LINK virtual COM port. A hosted CI runner only compiles these images;
it does not flash or attach hardware.

## Flash and debug

With the board attached through ST-LINK:

```bash
./k-fsw/tools/flash.sh nucleo_l496zg
./k-fsw/tools/debugserver.sh nucleo_l496zg
./k-fsw/tools/debug.sh nucleo_l496zg
```

The default shell is `/dev/ttyACM0` at 115200 baud. Override `KFSW_SERIAL` when
the stable device path differs:

```bash
KFSW_SERIAL=/dev/serial/by-id/<st-link-device> \
  ./k-fsw/tools/serial.sh nucleo_l496zg
```

Flashing and serial access are physical operations and are never part of the
GitHub-hosted software gate.

## Shell

The prompt identity is `kfsw`; commands are registered at the Zephyr shell
root. Use tab completion and `help` to discover commands.

```text
kfsw:~$ status
kfsw:~$ version
kfsw:~$ csp info
kfsw:~$ param list
kfsw:~$ storage info
```

See @ref commands for complete syntax and examples.
