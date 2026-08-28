# K Flight Software — K-FSW

[![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-K--FSW-168cff)](https://dgonzalez97.github.io/k-fsw/)

K-FSW is an open-source flight-software framework built on Zephyr RTOS. The
same application runs in native simulation and on the NUCLEO-L496ZG, with
reusable platform, service, and communications modules pinned in a west
workspace.

[Read the documentation](https://dgonzalez97.github.io/k-fsw/) for setup,
architecture, operations, testing, development, and the public C API.

## Current capabilities

- Zephyr shell on KFSW-Linux and NUCLEO-L496ZG
- CSP routing with UART/KISS and RDP
- typed parameters with persistent storage
- LittleFS storage and CSP/RDP file transfer
- ztest/Twister, Valgrind, Robot Framework, and physical HIL paths

## Try KFSW-Linux

From a configured west workspace root:

```bash
west manifest --validate
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

The application starts at the standard Zephyr prompt:

```text
kfsw:~$ status
kfsw:~$ csp ping 2
kfsw:~$ param get test_u32
kfsw:~$ storage info
```

## Supported profiles

| Profile | Target | Use |
| --- | --- | --- |
| `linux` | `native_sim/native/64` | Primary simulated node |
| `linux_node2` | `native_sim/native/64` | Software integration peer |
| `linux_uart` | `native_sim/native/64` | Physical UART bridge |
| `nucleo_l496zg` | NUCLEO-L496ZG | Base embedded image |
| `nucleo_l496zg_uart` | NUCLEO-L496ZG | Embedded UART/KISS node |

## Project layout

`k-fsw` composes the application and owns profiles, tools, integration tests,
and the aggregate documentation. Reusable code lives in three west-pinned
repositories:

| Repository | Responsibility |
| --- | --- |
| [`kfsw-platform`](https://github.com/dgonzalez97/kfsw-platform) | Zephyr-backed time, storage, and platform capabilities |
| [`kfsw-services`](https://github.com/dgonzalez97/kfsw-services) | Logging, parameters, persistence, and file-transfer services |
| [`kfsw-comms`](https://github.com/dgonzalez97/kfsw-comms) | CSP lifecycle, routing, and transports |

Exact dependency commits are recorded in [`west.yml`](west.yml).

## Development

Run the software-only CI sequence from the workspace root:

```bash
./k-fsw/tools/ci/all.sh
```

Build and serve the documentation locally:

```bash
./k-fsw/tools/docs/build.sh
./k-fsw/tools/docs/serve.sh
```

Generated HTML is written to `build/docs/html/`. Physical HIL is selected
explicitly and requires local hardware.

See the [development guide](https://dgonzalez97.github.io/k-fsw/development.html)
for contribution policy and coordinated west-pinned changes.
