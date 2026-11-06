# Zephyr integration {#zephyr_integration}

[TOC]

## Zephyr

Zephyr supplies the RTOS kernel, drivers, board descriptions, shell,
filesystems, and build tools used by K-FSW. Its native simulator runs the
application on Linux for service and integration tests.

Kconfig selects software; devicetree selects devices and wiring.
This lets the same services run on different MCU vendors without embedding
board names in service code.

K-FSW currently uses Zephyr APIs directly. Reusing modules and services on
another RTOS requires porting their kernel, device, storage, and build
dependencies as well as the platform layer.

## Kernel use

| Primitive | K-FSW use |
| --- | --- |
| Thread | Router, service workers, shell, application |
| Mutex | Shared parameter, persistence, storage, and client state |
| Workqueue | Deferred device work such as button debounce |
| Timeout and sleep | Bounded waits and periodic work |
| Spinlock | Short event-ring updates |

Calls that wait on a mutex, the filesystem or the network must run in a thread.
Check each public API before calling it from an ISR. UART reception ingests
bytes and leaves packet routing to the CSP router thread.

See Zephyr's [threads](https://docs.zephyrproject.org/4.4.0/kernel/services/threads/index.html)
and [mutexes](https://docs.zephyrproject.org/4.4.0/kernel/services/synchronization/mutexes.html).

## Devices

Devicetree identifies an instance; Zephyr creates its `struct device`.
Check `device_is_ready()` before using the driver API.

For example, a chosen UART selects the device consumed by the KISS adapter.
A chosen flash partition selects the LittleFS volume. K-FSW services use
Zephyr's filesystem calls once storage is mounted.

See the [device model](https://docs.zephyrproject.org/4.4.0/kernel/drivers/index.html).

## Kconfig

Configuration is combined at build time:

```text
app/prj.conf
  + app/boards/<board>.conf
  + optional profile .conf
  -> build/<target>/zephyr/.config
```

`CONFIG_KFSW_*` symbols select services and their startup and shell code.
Dependencies are checked by Kconfig: remote parameters need PARAM and CSP;
FTP needs CSP and storage.

Inspect the generated result from the workspace root:

```bash
rg '^CONFIG_KFSW_' build/linux/zephyr/.config
```

Change the source `.conf` or Kconfig file and rebuild.
See the [Kconfig guide](https://docs.zephyrproject.org/4.4.0/build/kconfig/index.html).

### Browsing the options

Zephyr ships the same terminal chooser the Linux kernel uses. It needs a build
directory that has already been configured, so build the target first:

```bash
source .venv/bin/activate
west build -t menuconfig -d build/nucleo_l496zg
```

Arrow keys move, space toggles, `/` searches the whole tree and `?` shows where
a symbol is defined and what depends on it. `west build -t guiconfig` opens the
same thing as a window instead, which needs an X display.

It lists every option the build knows, not only K-FSW's: `CONFIG_SHELL_STACK_SIZE`
and `CONFIG_CSP_BUFFER_COUNT` sit under their own subsystems. The K-FSW options
are five menus, one per repository:

```text
  K-FSW platform             kfsw-platform
  K-FSW communications       kfsw-comms
  K-FSW services             kfsw-services
  K-FSW reusable modules     kfsw-modules, one submenu per module
  K-FSW application          k-fsw/app
```

### Keeping what you chose

The chooser writes `build/<target>/zephyr/.config`, which is a build artefact. It
survives an incremental `west build` and reaches `autoconf.h`, so a change can be
tried straight away, but a pristine build regenerates it from the `.conf` files
and the choice is gone. `tools/build.sh` builds with `west build -p auto`, and
`tools/golden.sh` and CI use `always`.

So copy the file first, then compare afterwards. `diffconfig -m` prints the
difference in `.conf` format:

```bash
cp build/nucleo_l496zg/zephyr/.config /tmp/before
west build -t menuconfig -d build/nucleo_l496zg
python3 zephyr/scripts/kconfig/diffconfig -m \
  /tmp/before build/nucleo_l496zg/zephyr/.config
```

```text
CONFIG_KFSW_REMEXEC=y
CONFIG_KFSW_REMEXEC_CSP_PORT=8
CONFIG_KFSW_REMEXEC_MAX_ENTRIES=16
CONFIG_KFSW_REMEXEC_OUTPUT_MAX=200
CONFIG_KFSW_REMEXEC_SERVER_PRIORITY=3
CONFIG_KFSW_REMEXEC_SERVER_STACK_SIZE=3072
CONFIG_KFSW_REMEXEC_TIMEOUT_MS=5000
```

Turning one option on brings its defaults with it. Paste only the lines that are
a decision; the six defaults above belong in Kconfig, where they already are.

Then put the kept lines where that target or variant is composed: a board file
such as `app/boards/nucleo_l496zg.conf` for everything that target always has,
or a profile in `config/profiles/` for a variant. To find which file sets an
option today, read `build/<target>/zephyr/.config-trace.json`, where every
symbol carries its value and the file and line it came from.

### A composition with profiles

The chooser knows nothing about layering: it shows one resolved configuration.
To work on a layered one, build it with its profiles and then open that build
directory:

```bash
KFSW_EXTRA_CONF_FILE="$PWD/k-fsw/config/profiles/nucleo-can.conf;$PWD/k-fsw/config/profiles/nucleo-button-test.conf" \
  ./k-fsw/tools/build.sh nucleo_l496zg
west build -t menuconfig -d build/nucleo_l496zg
```

The comparison then shows what the layers already decided as well as what you
changed, so check each line against the profiles before copying it anywhere.

## Devicetree

Use overlays for hardware instances, pins, baud rates, and flash regions.
The NUCLEO UART/KISS profile uses:

```text
Kconfig:     CONFIG_KFSW_CSP_KISS_UART=y
chosen:      kfsw,csp-uart = &usart3
UART:        PD8/PD9, 115200 baud, 8N1
```

`kfsw,storage-partition` selects the storage partition. `kfsw,tmp-disk` selects
the `zephyr,ram-disk` behind `/kfsw/tmp`; the NUCLEO points it at SRAM2 with
`ram-region = <&sram1>`.
Multi-link profiles declare separate named interfaces; see @ref communications.

Inspect `build/<target>/zephyr/zephyr.dts` to confirm the final hardware
description. See the [devicetree guide](https://docs.zephyrproject.org/4.4.0/build/dts/index.html)
and [overlay how-to](https://docs.zephyrproject.org/4.4.0/build/dts/howtos.html).

## Boards and K-FSW targets

| K-FSW target | Zephyr board |
| --- | --- |
| `linux` | `native_sim/native/64` |
| `nucleo_l496zg` | `nucleo_l496zg` |
| `frdm_k64f` | `frdm_k64f/mk64f12` |
| `rpi_pico_w` | `rpi_pico/rp2040/w` |

`config/targets/<target>.env` maps the target to its board and tool defaults.
Board configuration and overlays live under `app/boards/`; optional
compositions live under `config/profiles/`.

See @ref targets for supported features and bench results. For new hardware,
start with Zephyr's [board-porting guide](https://docs.zephyrproject.org/4.4.0/hardware/porting/board_porting.html).

## west

`west update` checks out the revisions in `west.yml`.
`west build`, `west flash`, and `west debug` invoke Zephyr's tooling.

From the workspace root:

```bash
. .venv/bin/activate
west manifest --validate
west update
./k-fsw/tools/zephyr-patch.sh
```

Preserve local dependency changes before updating. Detached HEAD at a
manifest SHA is expected; @ref development covers dependency branches.

See the [west guide](https://docs.zephyrproject.org/4.4.0/develop/west/index.html).

## Build output

CMake resolves the board, Kconfig, devicetree, and module sources; Ninja
compiles and links the image.

| File under `build/<target>/` | Use |
| --- | --- |
| `zephyr/.config` | Resolved software selection |
| `zephyr/zephyr.dts` | Resolved devices and wiring |
| `zephyr/include/generated/` | Generated configuration headers |
| `compile_commands.json` | Compiler flags, include paths, IntelliSense |
| `zephyr/zephyr.elf` | Symbols and source debugging |

Regenerate these files by rebuilding. See the
[CMake build guide](https://docs.zephyrproject.org/4.4.0/build/cmake/index.html).
