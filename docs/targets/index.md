# Boards and Targets {#targets}

## Board and target are different concepts

Zephyr defines boards such as `nucleo_l496zg` and
`rpi_pico/rp2040/w`. A Zephyr board selects the SoC, CPU, devicetree, default
peripherals, and build/flash runners.

K-FSW defines target names such as `linux` and `rpi_pico_w`. A target descriptor
under `config/targets/` maps that convenient name to a Zephyr board and records
local tool defaults: serial device, baud rate, USB identity, expected prompt,
flash runner, and debugger endpoint where applicable.

```text
./k-fsw/tools/build.sh rpi_pico_w
                         |
       config/targets/rpi_pico_w.env
                         |
       ZEPHYR_BOARD=rpi_pico/rp2040/w
                         |
     Zephyr board + K-FSW board config/overlay
```

A target's default composition is part of its identity. Successfully building
some other Kconfig combination for the same Zephyr board does not silently add
that combination to the supported matrix.

## Status language

This manual uses these scopes:

- **Implemented**: merged source and configuration exist.
- **Software-tested**: an automated host/CI test exercises the behavior.
- **Physically verified**: a defined hardware acceptance path has passed.
- **Physical shell bring-up verified**: build, flash, prompt, and a small shell
  set passed; other services remain unqualified.
- **Planned**: an issue or current roadmap describes work that is not merged.

These scopes make no release or flight-qualification claim.

## Target matrix

| K-FSW target | Zephyr board | Default composition | Current qualification |
| --- | --- | --- | --- |
| `linux` | `native_sim/native/64` | Shell, storage, local PARAM, persistence, CSP UART/KISS, PARAM CSP adapter, FTP | Software-tested reference target |
| `nucleo_l496zg` | `nucleo_l496zg` | Same service set as Linux; shell on ST-LINK and CSP on USART3 | Hosted build plus physical boot, storage, UART/KISS, CSP, remote PARAM, and FTP bench verification |
| `frdm_k64f` | `frdm_k64f/mk64f12` | Shell and basic platform/boot paths; CSP, PARAM, persistence, storage, and FTP disabled | Physical shell bring-up verified |
| `rpi_pico_w` | `rpi_pico/rp2040/w` | USB CDC ACM shell and basic platform/boot paths; CSP, PARAM, persistence, storage, and FTP disabled | Physical shell bring-up verified |

Only Linux and NUCLEO-L496ZG are in the hosted `BUILD` matrix. FRDM and Pico
were accepted with the reusable manual physical shell runner; they are not
hosted service-qualification gates.

## KFSW-Linux

### Purpose

KFSW-Linux is the application built for Zephyr's 64-bit native simulator. It
is not a separate POSIX rewrite. It compiles the same project-owned platform,
service, communications, startup, and shell sources as the NUCLEO composition,
with Zephyr's simulated devices underneath them.

This makes it the fastest feedback target and lets tests run real service
interactions without hardware. It also makes limitations visible: simulated
execution does not verify MCU timing, electrical interfaces, interrupt load,
flash endurance, or toolchain-specific MCU behavior.

### Build and run

```bash
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

The executable is `build/linux/zephyr/zephyr.exe`. By default the runner gives
it `build/linux/kfsw-storage.bin` as persistent simulated flash. Use test
runners for isolated flash fixtures rather than sharing this developer image
between parallel scenarios.

The interactive shell uses stdin/stdout. A line such as:

```text
uart_1 connected to pseudotty: /dev/pts/...
```

reports the separate simulated CSP UART. It is not the shell console.

### What software tests prove

The hosted pipeline builds the target and a CSP-disabled minimal composition.
Twister tests individual modules on native simulation. Integration and Robot
tests start one or two full processes for shell, parameters, persistence,
LittleFS, CSP/KISS, RDP, and FTP. Valgrind checks bounded normal and corrupted
snapshot boots.

The `linux-node2` configuration under `tests/config/` is a peer fixture. It is
not another K-FSW product target.

## NUCLEO-L496ZG

### Default composition

The NUCLEO target is the current embedded reference. Its application profile
enables storage, local and remote parameters, persistence, CSP, UART/KISS, and
FTP. Internal flash is split into a 960 KiB application partition and 64 KiB
LittleFS partition.

The two UARTs have fixed roles:

| Path | Hardware | Use |
| --- | --- | --- |
| Debug console | ST-LINK virtual COM / LPUART1 | Zephyr shell, K-FSW logs, boot markers |
| CSP link | USART3 on PD8 TX and PD9 RX | 115200 8N1 KISS packets |

The CSP receive path is interrupt-driven. The current default addresses the
NUCLEO as node 2 and routes non-local traffic directly to the KISS interface.

### Build, flash, and console

```bash
./k-fsw/tools/build.sh nucleo_l496zg
./k-fsw/tools/flash.sh nucleo_l496zg
./k-fsw/tools/serial.sh nucleo_l496zg 30
```

Use a stable serial path when the default device name is unsuitable:

```bash
KFSW_SERIAL=/dev/serial/by-id/<st-link-device> \
  ./k-fsw/tools/serial.sh nucleo_l496zg 30
```

For source debugging, start the server in one terminal and the client in
another:

```bash
./k-fsw/tools/debugserver.sh nucleo_l496zg
./k-fsw/tools/debug.sh nucleo_l496zg
```

The scripts build automatically when the expected ELF is absent. They use the
target descriptor and Zephyr's OpenOCD runner; they do not encode service
behavior.

### Physical verification

The physical boot test checks ST-LINK visibility, builds/flashes, captures the
console before reset, and requires `@BOOT` and `@READY`.

The UART/KISS bench adds a 3.3 V FTDI cable and KFSW-Linux peer. It requires
bidirectional ping, UART interface status, mounted storage and its file test,
remote parameter access, 4 KiB and 16 KiB FTP round trips, and clean nonzero
KISS statistics. @ref communications describes the topology.

This evidence applies to the defined NUCLEO/FTDI bench. CSP over CAN and CSP
over radio are not implemented or physically verified.

## FRDM-K64F shell profile

The `frdm_k64f` target maps to Zephyr's `frdm_k64f/mk64f12` board and uses the
OpenSDA/DAPLink serial console and OpenOCD flash runner. Its K-FSW board
configuration explicitly disables CSP, parameters, persistence, FTP, storage,
flash-map, and filesystem support.

Build it with:

```bash
./k-fsw/tools/build.sh frdm_k64f
```

The merged physical shell acceptance path is:

```bash
KFSW_SERIAL=/dev/serial/by-id/<frdm-console> \
  ./k-fsw/tests/hil/shell-smoke.sh frdm_k64f
```

It builds, checks the configured USB flash device, flashes, waits for
`kfsw:~$`, and exercises `status`, `version`, and `help`. This is physical shell
bring-up verification only. It does not qualify CSP, a second UART, storage,
parameters, or FTP on the K64F.

## Raspberry Pi Pico W shell profile

The `rpi_pico_w` target maps to `rpi_pico/rp2040/w`. Its overlay creates a USB
CDC ACM console and selects it for the Zephyr console and shell. The shell
waits for host DTR so output is not discarded before a terminal is attached.

Like FRDM, the target disables CSP, parameters, persistence, FTP, storage, and
the flash filesystem. Wi-Fi is not configured; “Pico W” identifies the board,
not a supported K-FSW wireless transport.

Build and run the reusable physical acceptance path with:

```bash
./k-fsw/tools/build.sh rpi_pico_w
KFSW_SERIAL=/dev/serial/by-id/<pico-console> \
  ./k-fsw/tests/hil/shell-smoke.sh rpi_pico_w
```

The target selects Zephyr's UF2 runner. Where host/USB forwarding requires a
manual UF2 copy and device reattachment, flash first and run the same
acceptance behavior without another flash:

```bash
KFSW_FLASH=0 \
KFSW_SERIAL=/dev/serial/by-id/<pico-console> \
  ./k-fsw/tests/hil/shell-smoke.sh rpi_pico_w
```

The accepted scope is prompt, `status`, `version`, and `help`. USB CDC shell
success does not verify the RP2040 flash backend, Wi-Fi hardware, CSP, or any
full-service composition.

## Reusable shell behavior versus fixture

`tests/hil/shell-smoke.sh` owns the common acceptance behavior. A target `.env`
owns the fixture values.

```text
common behavior                       target fixture
---------------                       --------------
build                                 Zephyr board
optional flash          <----------   flash USB ID + runner
discover/open console   <----------   app USB ID / serial path / baud
wait for prompt         <----------   expected prompt
status/version/help                    
```

This split is what made FRDM and Pico bring-up additive. A future shell target
should supply a descriptor and only add script branches when the physical
fixture has genuinely different behavior.

## Adding or extending a target

A new K-FSW target normally requires:

1. an upstream or project-maintained Zephyr board target;
2. `config/targets/<name>.env` with the board mapping and tool fixture;
3. a normalized `app/boards/<board>.conf` defining the default composition;
4. an overlay when K-FSW must choose a UART, storage partition, or console;
5. a clean build;
6. software tests for reusable behavior; and
7. a physical acceptance test whose claims match the enabled services.

Enabling the full NUCLEO configuration on another board is not just a Kconfig
copy. Storage layout, erase behavior, UART electrical connection, interrupt
behavior, flash runner, console, and service resource sizes all require review
and physical evidence.
