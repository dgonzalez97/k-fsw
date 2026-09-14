# Getting started {#getting_started}

[TOC]

## Workspace

K-FSW is a west workspace. The `k-fsw` repository has the manifest and the
application; west checks out Zephyr and the other four K-FSW repositories next
to it.

The commands in this manual assume:

```text
k-fsw-workspace/                    workspace root
|-- .venv/                          Python environment, including west
|-- .west/                          west metadata
|-- k-fsw/                          manifest and application
|-- kfsw-platform/                  pinned dependency
|-- kfsw-services/                  pinned dependency
|-- kfsw-comms/                     pinned dependency
|   `-- third_party/libcsp/         pinned west project
|-- kfsw-modules/                   pinned dependency
|-- zephyr/                         pinned Zephyr tree
|-- modules/                        Zephyr modules
`-- build/                          build output
```

Run the project scripts from the workspace root; they activate `.venv`
themselves. Plain `west` commands need the environment activated, or
`.venv/bin/west`.

## Host requirements

CI uses Ubuntu 24.04 and Python 3.12. Other Linux distributions work if they
meet the Zephyr 4.4.0 requirements.

| Tool | Needed for |
| --- | --- |
| Git, CMake 3.20.5+, Ninja, devicetree compiler, host compiler | Workspace and native build |
| Python 3.12+, `venv`, west, Zephyr Python requirements | Configuration, build and tests |
| Zephyr SDK with `arm-zephyr-eabi` | MCU images |
| Doxygen | HTML manual and API |
| Pandoc and WeasyPrint | PDF manual |
| clang-format and cppcheck | Quality checks |
| Valgrind | Memory checks |
| socat and tmux | Two-node and Robot terminal tests |
| OpenOCD, USB access, serial tools | Flashing, debugging and hardware tests |

Follow the Zephyr
[getting started guide](https://docs.zephyrproject.org/4.4.0/develop/getting_started/index.html)
for the host packages and the SDK. On Ubuntu:

```bash
sudo apt install --no-install-recommends \
  git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget \
  python3-dev python3-venv python3-tk xz-utils file make gcc gcc-multilib \
  g++-multilib libsdl2-dev libmagic1
```

On an AArch64 host, leave out `gcc-multilib` and `g++-multilib`. Install a
Zephyr SDK that works with Zephyr 4.4.0, either where Zephyr finds it or with
`ZEPHYR_SDK_INSTALL_DIR` set.

## Create a workspace

In an empty directory, clone the manifest repository:

```bash
mkdir k-fsw-workspace
cd k-fsw-workspace
git clone https://github.com/dgonzalez97/k-fsw.git
```

Create the Python environment and install west:

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install west
```

Initialize west from the local manifest and check out the pinned revisions:

```bash
west init -l k-fsw
west update
```

Install Zephyr's Python requirements and export its CMake package:

```bash
python -m pip install -r zephyr/scripts/requirements.txt
west zephyr-export
west manifest --validate
```

Initialize the submodules:

```bash
git -C k-fsw submodule update --init --recursive
```

The submodules are the Robot terminal runner and the Yamcs configuration. The
application build doesn't need them.

## Update a workspace

When `k-fsw/west.yml` changes:

```bash
cd k-fsw-workspace
. .venv/bin/activate
git -C k-fsw switch main
git -C k-fsw pull --ff-only
west manifest --validate
west update
git -C k-fsw submodule update --init --recursive
```

`west update` may say it left a local branch behind, and usually leaves each
dependency on a detached `HEAD` at the manifest commit. That is normal. Commit
or stash your work in the dependencies first.

If the shell says `west: command not found`, activate `.venv` or run:

```bash
./.venv/bin/west update
```

## First KFSW-Linux build

```bash
./k-fsw/tools/kfsw-linux build
```

This builds the `linux` target (`native_sim/native/64`) into `build/linux/`.
The executable is:

```text
build/linux/zephyr/zephyr.exe
```

The generic command does the same:

```bash
./k-fsw/tools/build.sh linux
```

The script prints the target, board, output directory and pristine mode before
calling `west build`. Normal builds are incremental (`KFSW_PRISTINE=auto`).
After changing the board, toolchain or modules, force a full reconfigure:

```bash
KFSW_PRISTINE=always ./k-fsw/tools/build.sh linux
```

Don't edit `build/linux/zephyr/.config` by hand; change the Kconfig, `.conf` or
overlay and rebuild.

## Run KFSW-Linux

```bash
./k-fsw/tools/kfsw-linux run
```

The script builds if needed and starts the executable with a persistent flash
file. Startup looks like:

```text
[INFO] K-FSW application starting
...
@BOOT sw=v1.0.1 board=native_sim/native/64 ...
@READY uptime_ms=...

kfsw:~$
```

The native UART driver also prints a PTY for CSP. Keep using the current
terminal for the shell.

After `@READY`, try:

```text
kfsw:~$ status
kfsw:~$ version
kfsw:~$ time
kfsw:~$ storage info
kfsw:~$ param list
kfsw:~$ param get log_level
kfsw:~$ csp info
kfsw:~$ csp routes
```

Node 1 can't ping node 2 until a peer and a serial bridge are running.
`tests/csp-smoke.sh` and the Robot terminal suite set that up.

Press `Ctrl-C` to stop the node. The flash file is kept, so saved parameters
and FTP files are still there on the next run.

## Inspect a build

To see what a build was configured with:

```bash
grep '^CONFIG_KFSW_' build/linux/zephyr/.config
grep '^CONFIG_BOARD' build/linux/zephyr/.config
```

Other useful files:

```text
build/linux/compile_commands.json
build/linux/zephyr/zephyr.elf
build/linux/zephyr/zephyr.map
```

`version` and `status` print the board the image was built for.

## Build the reference targets

Build both CI targets from clean directories:

```bash
./k-fsw/tools/ci/build.sh
```

Or one at a time:

```bash
./k-fsw/tools/build.sh linux
./k-fsw/tools/build.sh nucleo_l496zg
```

CI only builds these two. The FRDM and Pico profiles build the same way:

```bash
./k-fsw/tools/build.sh frdm_k64f
./k-fsw/tools/build.sh rpi_pico_w
```

See @ref targets for what each profile includes.

## NUCLEO flash, serial and debug

Connect the NUCLEO through its ST-LINK, then build and flash:

```bash
./k-fsw/tools/build.sh nucleo_l496zg
./k-fsw/tools/flash.sh nucleo_l496zg
```

Capture 30 seconds of console output:

```bash
./k-fsw/tools/serial.sh nucleo_l496zg 30
```

Use `/dev/serial/by-id/` paths on a bench:

```bash
KFSW_SERIAL=/dev/serial/by-id/<st-link-device> \
  ./k-fsw/tools/serial.sh nucleo_l496zg 30
```

A terminal program needs 115200 8N1. This is the ST-LINK console; the CSP link
is on USART3, see @ref communications.

To debug with Zephyr's OpenOCD runner, start the server:

```bash
./k-fsw/tools/debugserver.sh nucleo_l496zg
```

Then, in another terminal:

```bash
./k-fsw/tools/debug.sh nucleo_l496zg
```

`debug.sh` starts GDB through west. A VS Code Cortex-Debug configuration can use
the same ELF and OpenOCD server instead.

## Software checks

While working, run the checks that cover your change:

```bash
./k-fsw/tools/ci/quality.sh
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/integration.sh
./k-fsw/tools/ci/valgrind.sh
./k-fsw/tools/ci/robot.sh
./k-fsw/tools/ci/docs.sh
```

Before opening a pull request, run all of them:

```bash
./k-fsw/tools/ci/all.sh
```

This doesn't use any hardware. See @ref testing for the hardware tests.

## Build the documentation

Install Doxygen for the HTML and the PDF dependencies in the workspace
environment:

```bash
./.venv/bin/pip install -r k-fsw/docs/pdf/requirements.txt
```

Build both:

```bash
./k-fsw/tools/docs/build.sh
./k-fsw/tools/docs/pdf.sh
```

The output is not committed:

```text
build/docs/html/index.html
build/docs/doxygen-warnings.log
build/k-fsw-guide.pdf
```

Serve the HTML from the workspace:

```bash
./k-fsw/tools/docs/serve.sh
```

The HTML includes the C API; the PDF only has the manual.

## Common problems

### west is not available

Activate the workspace environment:

```bash
. .venv/bin/activate
west --version
```

If `.venv` has no west, install it with that environment's Python.

### A pinned repository is missing

From the workspace root:

```bash
west manifest --validate
west update
```

Don't clone a branch into that directory by hand; west checks out the pinned
revision.

### The MCU toolchain is not found

Check the Zephyr SDK installation and set, for example:

```bash
export ZEPHYR_SDK_INSTALL_DIR=/path/to/zephyr-sdk
```

Then build with `KFSW_PRISTINE=always` so CMake picks up the new toolchain.

### A serial device changes name

Use a `/dev/serial/by-id/` path and export `KFSW_SERIAL`, or the variable the
test asks for. Check the group permissions and that no other terminal has the
device open.

### A command is missing

Commands depend on Kconfig. Check the target `.conf` and the generated
`.config`. The FRDM and Pico profiles have no CSP, parameter, storage, UART or
FTP commands.

### Old state in storage

The Linux runner keeps its flash file between runs; tests use their own files.
For a clean start, move `build/linux/kfsw-storage.bin` aside or use the
simulator's flash erase options.

## Next

Read @ref architecture and @ref zephyr_integration before adding a service or
target, @ref development before changing a dependency, and @ref commands for
the shell.
