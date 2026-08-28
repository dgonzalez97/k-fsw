# Getting Started {#getting_started}

K-FSW uses a west workspace. The `k-fsw` repository contains the application,
manifest, profiles, and developer tools; west checks out Zephyr and the K-FSW
module repositories beside it.

The commands in this chapter use the following layout:

```text
k-fsw-workspace/
├── k-fsw/
├── kfsw-platform/
├── kfsw-services/
├── kfsw-comms/
├── zephyr/
└── build/
```

Run project commands from `k-fsw-workspace/`, which is referred to below as the
workspace root.

## Requirements

The reference build is verified by CI on Ubuntu 24.04 with Python 3.12. Other
Linux distributions can be used when they provide the tools required by
Zephyr 4.4.0.

| Item | Requirement |
| --- | --- |
| Host tools | CMake 3.20.5+, Python 3.12+, devicetree compiler 1.4.6+, Git, and Ninja |
| Workspace tool | west |
| RTOS revision | Zephyr 4.4.0, pinned by `west.yml` |
| Native build | Host C/C++ toolchain for `native_sim/native/64` |
| Embedded build | Zephyr SDK with the `arm-zephyr-eabi` toolchain |
| Board tools | OpenOCD and ST-LINK access for flash and debug |

On Ubuntu, install the host packages used by Zephyr with:

```bash
sudo apt install --no-install-recommends \
  git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget \
  python3-dev python3-venv python3-tk xz-utils file make gcc gcc-multilib \
  g++-multilib libsdl2-dev libmagic1
```

On an AArch64 host, omit `gcc-multilib` and `g++-multilib`, which are not
available for that architecture.

Install a Zephyr SDK compatible with Zephyr 4.4.0 before building the NUCLEO
profiles. If it is not installed at a location detected by Zephyr, set
`ZEPHYR_SDK_INSTALL_DIR` in the shell used for the build.

## Create the workspace

1. Create a workspace directory and enter it:

   ```bash
   mkdir k-fsw-workspace
   cd k-fsw-workspace
   ```

2. Clone the manifest repository:

   ```bash
   git clone https://github.com/dgonzalez97/k-fsw.git
   ```

3. Create the Python environment and install west:

   ```bash
   python3 -m venv .venv
   . .venv/bin/activate
   pip install west
   ```

4. Initialize and update the west workspace:

   ```bash
   west init -l k-fsw
   west update
   ```

5. Install the Python requirements selected by the pinned Zephyr revision:

   ```bash
   pip install -r zephyr/scripts/requirements.txt
   ```

6. Export the Zephyr CMake package and validate the manifest:

   ```bash
   west zephyr-export
   west manifest --validate
   ```

The K-FSW scripts activate `.venv` automatically when it is present at the
workspace root.

## First KFSW-Linux build

KFSW-Linux is the reference application built for Zephyr native simulation. It
uses the same platform, service, communications, and shell modules as the
embedded profiles.

1. Change to the workspace root and activate the environment if it is not
   already active:

   ```bash
   cd k-fsw-workspace
   . .venv/bin/activate
   ```

2. Validate the manifest:

   ```bash
   west manifest --validate
   ```

3. Build the `linux` profile:

   ```bash
   ./k-fsw/tools/kfsw-linux build
   ```

The wrapper prints the selected profile, board, output directory, and pristine
mode before calling `west build`. The executable is written to:

```text
build/linux/zephyr/zephyr.exe
```

## Run KFSW-Linux

Start the application in the current terminal:

```bash
./k-fsw/tools/kfsw-linux run
```

If the executable is missing, the run command builds it first. A normal start
prints the automation markers and then the Zephyr shell prompt:

```text
@BOOT sw=kfsw-dev board=native_sim/native/64 ...
@READY uptime_ms=...

kfsw:~$
```

The line beginning `uart_1 connected to pseudotty:` identifies the simulated
CSP KISS transport. It is not the interactive shell.

Try a few read-only commands:

```text
kfsw:~$ status
kfsw:~$ version
kfsw:~$ param get test_u32
kfsw:~$ storage info
kfsw:~$ csp info
```

Press `Ctrl-C` to stop the node.

## Rebuild KFSW-Linux

For normal development, run the same build command again:

```bash
./k-fsw/tools/kfsw-linux build
```

The default pristine mode is `auto`, so Zephyr decides whether the existing
build directory can be reused. Force a clean build when changing toolchains or
working through stale CMake state:

```bash
KFSW_PRISTINE=always ./k-fsw/tools/kfsw-linux build
```

Build every supported software and embedded profile from clean directories
with the CI entry point:

```bash
./k-fsw/tools/ci/build.sh
```

## Build the NUCLEO-L496ZG image

Two profiles are available for the NUCLEO-L496ZG:

| Profile | Communications |
| --- | --- |
| `nucleo_l496zg` | Base image |
| `nucleo_l496zg_uart` | CSP KISS on USART3, PD8/PD9, at 115200 baud |

Build either profile from the workspace root:

```bash
./k-fsw/tools/build.sh nucleo_l496zg
./k-fsw/tools/build.sh nucleo_l496zg_uart
```

The shell and logs use the ST-LINK virtual COM port. The UART/KISS profile keeps
the CSP transport on the separate USART3 connection.

## Flash and inspect a board

1. Connect the board through ST-LINK.

2. Build and flash the selected profile:

   ```bash
   ./k-fsw/tools/build.sh nucleo_l496zg
   ./k-fsw/tools/flash.sh nucleo_l496zg
   ```

3. Capture 30 seconds of serial output from the default `/dev/ttyACM0` device:

   ```bash
   ./k-fsw/tools/serial.sh nucleo_l496zg 30
   ```

   Use a stable device path when the board is assigned a different name:

   ```bash
   KFSW_SERIAL=/dev/serial/by-id/<st-link-device> \
     ./k-fsw/tools/serial.sh nucleo_l496zg 30
   ```

4. Start an OpenOCD server and attach the debugger when source-level debugging
   is needed:

   ```bash
   ./k-fsw/tools/debugserver.sh nucleo_l496zg
   ./k-fsw/tools/debug.sh nucleo_l496zg
   ```

Flashing, serial capture, and debugging require local hardware and are not part
of the GitHub-hosted software gate.

## Next steps

- See @ref commands for shell syntax and examples.
- See @ref testing for the local CI and HIL commands.
- See @ref development before changing dependency pins or opening a pull
  request.
