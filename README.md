# K Flight Software — K-FSW

[![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-K--FSW-28a96b)](https://dgonzalez97.github.io/k-fsw/)

K-FSW is an open-source, modular flight-software framework for next space.
The current reference integration supports Zephyr RTOS in native simulation
and on the NUCLEO-L496ZG. Platform, service, and communications modules are
versioned independently and pinned in a west workspace.

CSP support lives in the optional `kfsw-comms` module. Each supported target
has a tested default configuration, while Kconfig and devicetree keep the
transport selectable for other compositions.

[Read the documentation](https://dgonzalez97.github.io/k-fsw/) for setup,
architecture, operations, testing, development, and the public C API.

## Current capabilities

- Zephyr shell on KFSW-Linux and NUCLEO-L496ZG
- optional CSP routing with UART/KISS and RDP
- typed parameters with persistent storage
- LittleFS storage and optional CSP/RDP file transfer
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

## Supported targets

| K-FSW target | Zephyr board | Default configuration |
| --- | --- | --- |
| `linux` | `native_sim/native/64` | CSP UART/KISS over a simulated PTY, parameters, persistence, storage, and FTP |
| `nucleo_l496zg` | `nucleo_l496zg` | CSP UART/KISS on USART3, parameters, persistence, storage, and FTP |

Physical shell bring-up profiles are intentionally narrower and are not yet
fully qualified K-FSW targets:

| K-FSW target | Zephyr board | Verified scope |
| --- | --- | --- |
| `frdm_k64f` | `frdm_k64f/mk64f12` | OpenSDA UART shell: prompt, `status`, `version`, and `help` |
| `rpi_pico_w` | `rpi_pico/rp2040/w` | USB CDC ACM shell: prompt, `status`, `version`, and `help` |

The second Linux CSP node is an integration-test configuration under
`tests/config/`; it is not another supported target.

## Project layout

`k-fsw` composes the application and owns targets, tools, integration tests,
and the aggregate documentation. Reusable code lives in three west-pinned
repositories. Their pinned revisions are validated together by K-FSW's
Software CI workflow.

| Repository | Responsibility | CI status |
| --- | --- | --- |
| [`k-fsw`](https://github.com/dgonzalez97/k-fsw) | Application composition, supported targets, integration tests, documentation, and CI | [![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml) |
| [`kfsw-platform`](https://github.com/dgonzalez97/kfsw-platform) | Zephyr-backed time, storage, and platform capabilities | [![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml) |
| [`kfsw-services`](https://github.com/dgonzalez97/kfsw-services) | Logging, parameters, persistence, and file-transfer services | [![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml) |
| [`kfsw-comms`](https://github.com/dgonzalez97/kfsw-comms) | CSP lifecycle, routing, and transports | [![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml) |

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

Build the printable SDK guide after installing its Python requirements:

```bash
./.venv/bin/pip install -r k-fsw/docs/pdf/requirements.txt
./k-fsw/tools/docs/pdf.sh
```

Generated HTML is written to `build/docs/html/`; the printable guide is
written to `build/k-fsw-guide.pdf`. Physical HIL is selected explicitly and
requires local hardware.

See the [development guide](https://dgonzalez97.github.io/k-fsw/development.html)
for contribution policy and coordinated west-pinned changes.
