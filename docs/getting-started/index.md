# Getting Started {#getting_started}

## Workspace model

K-FSW is developed as a west workspace. The `k-fsw` repository is the
manifest and application-composition repository; west checks out Zephyr and
the three reusable K-FSW repositories beside it.

Commands in this manual assume:

```text
k-fsw-workspace/                    workspace root
├── .venv/                          Python environment, including west
├── .west/                          west workspace metadata
├── k-fsw/                          manifest/application repository
├── kfsw-platform/                  pinned dependency
├── kfsw-services/                  pinned dependency
├── kfsw-comms/                     pinned dependency
│   └── third_party/libcsp/          separate pinned west project
├── zephyr/                         pinned Zephyr tree
├── modules/                        Zephyr-imported projects
└── build/                          generated output
```

Run project scripts from the workspace root. The scripts resolve their own
locations and automatically source `.venv/bin/activate` when it exists.
Direct `west` commands require either an activated environment or the explicit
path `.venv/bin/west`.

## Host requirements

Hosted CI uses Ubuntu 24.04 and Python 3.12. Other Linux distributions can
work when they satisfy Zephyr 4.4.0 and the selected board/toolchain
requirements.

| Area | Required for |
| --- | --- |
| Git, CMake 3.20.5+, Ninja, devicetree compiler, host compiler | Workspace and native build |
| Python 3.12+, `venv`, west, Zephyr Python requirements | Configuration, build, test tooling |
| Zephyr SDK with `arm-zephyr-eabi` | MCU images |
| Doxygen | HTML manual/API build |
| Pandoc and WeasyPrint | Printable PDF |
| clang-format and cppcheck | Static quality gate |
| Valgrind | Native memory gate |
| socat and tmux | Two-node integration and Robot terminal scenarios |
| OpenOCD, USB access, serial tools | Physical board flash/debug/HIL |

The Zephyr
[getting-started guide](https://docs.zephyrproject.org/4.4.0/develop/getting_started/index.html)
is authoritative for supported host dependencies and SDK installation. A
typical Ubuntu package baseline is:

```bash
sudo apt install --no-install-recommends \
  git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget \
  python3-dev python3-venv python3-tk xz-utils file make gcc gcc-multilib \
  g++-multilib libsdl2-dev libmagic1
```

On an AArch64 host, omit `gcc-multilib` and `g++-multilib`. Install a Zephyr
SDK compatible with Zephyr 4.4.0 and either place it where Zephyr detects it or
export `ZEPHYR_SDK_INSTALL_DIR`.

## Create a workspace

Choose an empty parent directory and clone the manifest repository:

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

Initialize west from the local manifest and resolve all exact pins:

```bash
west init -l k-fsw
west update
```

Install the Python requirements selected by the pinned Zephyr tree and export
Zephyr's CMake package:

```bash
python -m pip install -r zephyr/scripts/requirements.txt
west zephyr-export
west manifest --validate
```

Initialize the test-only Robot terminal runner submodule:

```bash
git -C k-fsw submodule update --init --recursive
```

That submodule is not flight code and is not managed by `west.yml`; both steps
are therefore required for a complete development/test workspace.

## Update an existing workspace

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

`west update` may print that it left a local dependency branch behind and will
normally detach dependency `HEAD` at the manifest commit. This is expected.
Before updating, commit or otherwise preserve intentional work in every
dependency; west must not be used as a substitute for understanding a dirty
working tree.

If the shell reports `west: command not found`, activate `.venv` or call:

```bash
./.venv/bin/west update
```

K-FSW's build/test scripts activate the environment automatically, but an
unwrapped `west update` cannot do that for itself.

## First KFSW-Linux build

The wrapper is the easiest entry point:

```bash
./k-fsw/tools/kfsw-linux build
```

It selects the K-FSW `linux` target, which maps to
`native_sim/native/64`, and writes generated files to `build/linux/`. The
executable is:

```text
build/linux/zephyr/zephyr.exe
```

The equivalent generic command is:

```bash
./k-fsw/tools/build.sh linux
```

The wrapper prints the target, Zephyr board, output directory, and pristine
mode before invoking `west build`. A normal incremental build uses
`KFSW_PRISTINE=auto`. Force full CMake reconfiguration after changing a board,
toolchain, module set, or confusing generated state:

```bash
KFSW_PRISTINE=always ./k-fsw/tools/build.sh linux
```

Do not manually repair `build/linux/zephyr/.config`. Fix the relevant Kconfig,
`.conf`, devicetree overlay, or build input and regenerate it.

## Run KFSW-Linux

```bash
./k-fsw/tools/kfsw-linux run
```

The wrapper builds first when necessary and starts the native executable with
a persistent simulated flash image. Normal startup contains:

```text
[INFO] K-FSW application starting
...
@BOOT sw=kfsw-dev board=native_sim/native/64 ...
@READY uptime_ms=...

kfsw:~$
```

The native UART driver also reports a pseudoterminal for CSP. That PTY belongs
to KISS transport automation; keep using the current terminal for shell
commands.

After `@READY`, try read-only commands:

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

Node 1 cannot ping node 2 until a peer and serial bridge are running. Use
`tests/csp-smoke.sh` or the Robot terminal suite for that topology rather than
manually guessing PTY paths.

Press `Ctrl-C` to stop a normal interactive node. Its default simulated flash
file remains, so explicitly saved parameters and FTP files can survive the
next run.

## Inspect the resolved build

When behavior differs from an expected target, inspect the generated inputs:

```bash
grep '^CONFIG_KFSW_' build/linux/zephyr/.config
grep '^CONFIG_BOARD' build/linux/zephyr/.config
```

The compilation database and ELF are useful diagnostics:

```text
build/linux/compile_commands.json
build/linux/zephyr/zephyr.elf
build/linux/zephyr/zephyr.map
```

The application prints its Zephyr board target through `version` and `status`.
This is more reliable than assuming a build directory still contains the
configuration implied by its name.

## Build the full reference targets

Build both hosted-CI targets from clean directories:

```bash
./k-fsw/tools/ci/build.sh
```

Or build them individually:

```bash
./k-fsw/tools/build.sh linux
./k-fsw/tools/build.sh nucleo_l496zg
```

`tools/ci/build.sh` defaults to only these two full reference targets. The
FRDM and Pico shell profiles use the generic builder but are not in the hosted
matrix:

```bash
./k-fsw/tools/build.sh frdm_k64f
./k-fsw/tools/build.sh rpi_pico_w
```

See @ref targets before interpreting a successful shell-profile build as
service support.

## NUCLEO flash, serial, and debug loop

Connect the NUCLEO through ST-LINK, then build and flash:

```bash
./k-fsw/tools/build.sh nucleo_l496zg
./k-fsw/tools/flash.sh nucleo_l496zg
```

Capture 30 seconds of the default console:

```bash
./k-fsw/tools/serial.sh nucleo_l496zg 30
```

Prefer `/dev/serial/by-id/` for repeatable benches:

```bash
KFSW_SERIAL=/dev/serial/by-id/<st-link-device> \
  ./k-fsw/tools/serial.sh nucleo_l496zg 30
```

For an interactive serial program, configure 115200 baud, 8 data bits, no
parity, and one stop bit. This console is the ST-LINK shell/log path. The CSP
KISS path is separate USART3 wiring; @ref communications covers the physical
bench.

Source debugging uses Zephyr's OpenOCD runner. Start the server:

```bash
./k-fsw/tools/debugserver.sh nucleo_l496zg
```

Then, in another terminal:

```bash
./k-fsw/tools/debug.sh nucleo_l496zg
```

`debug.sh` launches the configured GDB client through west. It is not required
when using a VS Code Cortex-Debug launch configuration, but the same ELF,
OpenOCD server, and generated source paths apply.

## Run focused software checks

Use the narrowest relevant check during iteration:

```bash
./k-fsw/tools/ci/quality.sh
./k-fsw/tools/ci/unit.sh
./k-fsw/tools/ci/integration.sh
./k-fsw/tools/ci/valgrind.sh
./k-fsw/tools/ci/robot.sh
./k-fsw/tools/ci/docs.sh
```

Run the complete software-only sequence before opening a composition pull
request:

```bash
./k-fsw/tools/ci/all.sh
```

It does not access physical boards. HIL is always an explicit command. See
@ref testing for prerequisites, artifacts, and proof boundaries.

## Build documentation

Install Doxygen for HTML. Install the printable guide dependencies into the
workspace environment:

```bash
./.venv/bin/pip install -r k-fsw/docs/pdf/requirements.txt
```

Build both outputs:

```bash
./k-fsw/tools/docs/build.sh
./k-fsw/tools/docs/pdf.sh
```

Outputs are generated, not committed:

```text
build/docs/html/index.html
build/docs/doxygen-warnings.log
build/k-fsw-guide.pdf
```

Serve HTML from the workspace with:

```bash
./k-fsw/tools/docs/serve.sh
```

The Doxygen site includes the generated C API. The PDF deliberately contains
only the engineering manual.

## Common failures

### `west` is not available

Activate the workspace environment:

```bash
. .venv/bin/activate
west --version
```

If `.venv` has no west executable, install it with that environment's Python.

### A pinned repository is missing

From the workspace root:

```bash
west manifest --validate
west update
```

Do not clone an arbitrary branch into the expected path; the build is defined
by the manifest revision.

### An MCU toolchain is not found

Verify the Zephyr SDK installation and set, for example:

```bash
export ZEPHYR_SDK_INSTALL_DIR=/path/to/zephyr-sdk
```

Then force a pristine build so CMake does not retain the previous toolchain
decision.

### A serial device changes name

Use a stable `/dev/serial/by-id/` path and export `KFSW_SERIAL` (or the
HIL-specific variable documented by the test). Check group permissions and
whether another terminal already owns the device.

### A service command is missing

Command registration follows Kconfig. Inspect the target `.conf` and generated
`.config`. The FRDM and Pico shell profiles intentionally omit CSP, PARAM,
storage, UART/KISS, and FTP command domains.

### Storage contains old developer state

The normal Linux runner preserves its flash image. Tests use isolated files.
When a clean developer image is genuinely intended, use the native simulator's
explicit flash erase/remove options or move the specific
`build/linux/kfsw-storage.bin` aside. Do not delete the workspace or an entire
build tree as a first response to one stateful fixture.

## Where to go next

Read @ref architecture and @ref zephyr_integration before adding a service or
target. Read @ref development before touching a west-managed dependency, and
read @ref commands before using write/persistence or file-transfer operations.
