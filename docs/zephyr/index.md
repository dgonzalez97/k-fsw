# Zephyr Integration {#zephyr_integration}

## What Zephyr provides

[Zephyr](https://docs.zephyrproject.org/4.4.0/) is a real-time operating system
and embedded software ecosystem. It supplies the kernel, thread and
synchronization primitives, device model, drivers, filesystems, shell,
configuration system, board descriptions, build system, and host simulation
used by the current K-FSW reference integration.

K-FSW uses those mechanisms; it does not rename or reimplement them. The
project-owned platform and service APIs exist where K-FSW needs a stable
semantic boundary, shared lifecycle, or policy. A board driver remains a
Zephyr driver. A filesystem file remains a Zephyr `fs_file_t`. A service that
only needs to open a file after storage is mounted does not gain anything from
a second wrapper around every `fs_*` call.

```text
                K-FSW application
                       |
                    services
                 /             \
            platform           comms
                 \             /
                    Zephyr APIs
                       |
                  device model
                       |
                     drivers
                       |
                    hardware
```

Zephyr is the current platform integration, not the definition of flight
software. Mission policy, parameter semantics, file-transfer rules, CSP
ownership, and repository composition remain K-FSW concerns.

## Kernel concepts used by K-FSW

Zephyr schedules threads by priority and readiness. The K-FSW application has
the normal main thread, the Zephyr shell infrastructure, and service-owned
threads such as the CSP router and FTP acceptor/worker. Mutexes protect shared
parameter, persistence, storage, and client workspaces. Sleep and timeout APIs
use the kernel's monotonic time base.

The useful distinction for current development is:

- a **thread** owns an independent execution context and stack;
- a **mutex** serializes access to shared state and may wait for its owner;
- a **queue or socket receive wait** blocks a worker until work arrives; and
- a **sleep or timeout** yields execution until monotonic time advances.

K-FSW service calls that take a mutex or perform filesystem/network work are
not suitable for an interrupt service routine. The current implementation does
not expose an ISR-safe application-service API. libcsp's interrupt-driven UART
receive path does only bounded byte ingestion and leaves packet routing to the
router thread.

The upstream [thread documentation](https://docs.zephyrproject.org/4.4.0/kernel/services/threads/index.html)
and [mutex documentation](https://docs.zephyrproject.org/4.4.0/kernel/services/synchronization/mutexes.html)
cover the relevant scheduling and synchronization contracts.

## Devices and drivers

Zephyr represents a configured hardware instance with a `struct device`.
Drivers register device instances during system initialization; application
code checks `device_is_ready()` before use and then calls the relevant driver
API. K-FSW's UART/KISS integration follows this model: devicetree selects a
UART, Zephyr creates its device, and `kfsw-comms` uses the standard UART API.

```text
devicetree node &usart3
          |
    Zephyr device instance
          |
      UART driver API
          |
 kfsw-comms KISS adapter
```

The same relationship applies to flash. A fixed-partition devicetree node is
resolved through Zephyr's flash-map API; LittleFS mounts that partition; K-FSW
services use the mounted volume.

The [device driver model](https://docs.zephyrproject.org/4.4.0/kernel/drivers/index.html)
documents initialization, device handles, and driver APIs. Driver-specific
pages should be consulted when porting a target or choosing interrupt, DMA, or
polling behavior.

## Kconfig: selecting software

Kconfig describes which software capabilities are built and validates their
dependencies. Symbols are resolved during CMake configuration and emitted to
the generated `.config` and `autoconf.h`; C source then uses
`CONFIG_KFSW_*` conditions to include the matching lifecycle and command code.

K-FSW has three configuration layers:

```text
app/prj.conf                     common application baseline
       +
app/boards/<zephyr-board>.conf  board/default composition
       +
optional --extra-conf           test or developer override
       |
       v
build/<target>/zephyr/.config   final resolved configuration
```

`app/prj.conf` enables the serial console, shell, logging, reset-cause support,
and the common storage prerequisites. A board configuration then enables or
disables CSP, parameters, persistence, FTP, and storage for that target. Test
scripts use `--extra-conf` for focused compositions such as local parameters
without CSP.

Dependencies are contracts, not documentation hints. For example,
`KFSW_PARAM_CSP` cannot be selected unless both `KFSW_PARAM` and `KFSW_CSP`
are enabled. `KFSW_FTP` requires CSP and storage. If Kconfig rejects a
combination, do not work around it with preprocessor definitions.

Use the generated result when diagnosing a build:

```bash
grep '^CONFIG_KFSW_' build/linux/zephyr/.config
```

The upstream [Kconfig guide](https://docs.zephyrproject.org/4.4.0/build/kconfig/index.html)
explains symbol types, dependencies, defaults, and configuration interfaces.

## Devicetree: describing hardware

Devicetree describes hardware topology and concrete instances. It answers
questions such as which UART carries CSP, which pins it uses, and which flash
partition backs storage. Kconfig answers whether the corresponding capability
is compiled.

That division is visible in the NUCLEO target:

```text
Kconfig
  CONFIG_KFSW_CSP_KISS_UART=y       enable the UART/KISS capability

Devicetree overlay
  kfsw,csp-uart = &usart3           select the device
  USART3 PD8/PD9, 115200, 8N1       describe the wiring
```

The project-owned chosen properties are:

- `kfsw,csp-uart`, the dedicated UART consumed by `kfsw-comms`; and
- `kfsw,storage-partition`, the fixed partition mounted by
  `kfsw-platform`.

They avoid board names and raw addresses in reusable C code. A new target
supplies its own overlay instead of adding another `#ifdef BOARD_*` branch to a
service.

The upstream [devicetree guide](https://docs.zephyrproject.org/4.4.0/build/dts/index.html)
explains nodes, properties, bindings, aliases, chosen nodes, and generated C
macros. The [overlay how-to](https://docs.zephyrproject.org/4.4.0/build/dts/howtos.html)
covers overlay selection and inspection.

## Boards, targets, and overlays

A **Zephyr board target** selects an upstream board, SoC, CPU, runners, and
base hardware description. A **K-FSW target** is a project-facing name that
maps to a Zephyr board plus K-FSW defaults and local tooling metadata.

For example:

```text
K-FSW target                 Zephyr board target
-------------                -------------------
linux             --------> native_sim/native/64
nucleo_l496zg     --------> nucleo_l496zg
frdm_k64f         --------> frdm_k64f/mk64f12
rpi_pico_w        --------> rpi_pico/rp2040/w
```

`config/targets/<target>.env` contains the mapping and local flash/serial
defaults. Zephyr then automatically discovers a normalized board `.conf` and
`.overlay` under `app/boards/`. @ref targets records the capability and
verification level of each target.

The upstream [board-porting guide](https://docs.zephyrproject.org/4.4.0/hardware/porting/board_porting.html)
is the next reference when an upstream board does not already exist.

## west and the workspace

[west](https://docs.zephyrproject.org/4.4.0/develop/west/index.html) is
Zephyr's workspace and command-line tool. Its manifest support checks out the
repositories named in `west.yml`; its extension commands drive builds,
flashing, debugging, and Twister.

K-FSW uses west in two distinct roles:

1. `west update` makes the multi-repository workspace match the exact manifest
   revisions.
2. `west build`, `west flash`, `west debug`, and `west twister` invoke Zephyr
   development commands against that workspace.

Run `west update` from the workspace root, not from one dependency checkout.
The workspace environment normally lives at `.venv`; project scripts source
it automatically. For a direct command use:

```bash
. .venv/bin/activate
west manifest --validate
west update
```

After an update, dependency repositories are normally in detached-HEAD state
at their manifest SHA. That is expected and is discussed in @ref development.

## CMake and generated build state

Zephyr's CMake package consumes Kconfig, devicetree, modules, the selected
board, and the application's sources to produce a Ninja build. K-FSW adds the
three owned repositories and libcsp to `ZEPHYR_EXTRA_MODULES` before calling
`find_package(Zephyr)`. Each module's `zephyr/module.yml` exposes its Kconfig
and CMake integration.

```text
target + .conf + overlay + west modules
                    |
             CMake configure
          /         |          \
      Kconfig    devicetree   module CMake
          \         |          /
             generated headers
                    |
               Ninja compile
                    |
     zephyr.elf / zephyr.bin / runners
```

Important generated artifacts include:

- `build/<target>/zephyr/.config`, the resolved Kconfig state;
- `build/<target>/zephyr/include/generated/`, generated configuration and
  devicetree headers;
- `build/<target>/compile_commands.json`, exact compiler invocations used by
  IntelliSense and analysis; and
- `build/<target>/zephyr/zephyr.elf`, the debuggable image.

Generated files are diagnostic outputs, not source. Change the owning `.conf`,
Kconfig, devicetree, or CMake file and rebuild rather than editing build output.
The upstream [CMake build-system guide](https://docs.zephyrproject.org/4.4.0/build/cmake/index.html)
describes the complete configuration and build phases.

## What K-FSW developers normally change

Most feature work touches one or more of these boundaries:

- service behavior and public interfaces in `kfsw-services`;
- platform lifecycle or cross-target semantics in `kfsw-platform`;
- CSP interfaces, routes, or ownership in `kfsw-comms`;
- enablement, target defaults, startup order, shell integration, or
  end-to-end tests in `k-fsw`.

Use Kconfig for software selection, devicetree for hardware description, and
the composition repository for product policy. Keeping those roles distinct is
what lets the same application run on native simulation and a microcontroller
without turning reusable code into a collection of board-specific branches.
