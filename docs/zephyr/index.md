# Zephyr integration {#zephyr_integration}

[TOC]

## Why Zephyr?

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

Calls that wait on a mutex, filesystem, or network belong in thread context.
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

## Devicetree

Use overlays for hardware instances, pins, baud rates, and flash regions.
The NUCLEO UART/KISS profile uses:

```text
Kconfig:     CONFIG_KFSW_CSP_KISS_UART=y
chosen:      kfsw,csp-uart = &usart3
UART:        PD8/PD9, 115200 baud, 8N1
```

`kfsw,storage-partition` selects the storage partition.
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
