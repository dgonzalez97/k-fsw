# K Flight Software — K-FSW

[![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-K--FSW-28a96b)](https://dgonzalez97.github.io/k-fsw/)

K-FSW is an open-source, modular flight-software framework. The full reference
composition runs on Zephyr in native simulation and on the NUCLEO-L496ZG.
FRDM-K64F and Raspberry Pi Pico W have narrower, physically verified shell
bring-up profiles. Platform, service, and communications modules are versioned
independently and pinned in a west workspace.

CSP support lives in the optional `kfsw-comms` module. Each supported target
has a tested default configuration, while Kconfig and devicetree keep the
transport selectable for other compositions.

[Read the documentation](https://dgonzalez97.github.io/k-fsw/) for setup,
architecture, operations, testing, development, and the public C API.

## Current capabilities

- full reference application on KFSW-Linux and NUCLEO-L496ZG
- physical shell bring-up on FRDM-K64F and Raspberry Pi Pico W
- configurable `k-ground` Linux nodes with local two-node CSP verification
- optional CSP routing with UART/KISS and RDP
- local typed parameters and persistence without a CSP dependency
- optional remote parameter access over CSP
- LittleFS storage and optional CSP/RDP file transfer
- hosted software CI plus manually invoked physical HIL paths

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

Physical shell bring-up profiles are intentionally narrower and are not
full-service K-FSW targets:

| K-FSW target | Zephyr board | Verified scope |
| --- | --- | --- |
| `frdm_k64f` | `frdm_k64f/mk64f12` | OpenSDA UART shell: prompt, `status`, `version`, and `help` |
| `rpi_pico_w` | `rpi_pico/rp2040/w` | USB CDC ACM shell: prompt, `status`, `version`, and `help` |

The second Linux CSP node is an integration-test configuration under
`tests/config/`; it is not another supported target.

## Try k-ground

Run an interactive ground node 16 linked to local peer 17:

```bash
./k-fsw/tools/k-ground demo
```

The prompt is `k-ground#`; `status`, `version`, and existing `csp` commands are
unchanged. See the [ground composition guide](docs/ground/index.md) for
standalone launch options and the pending Holybro HIL boundary.

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

Build the printable engineering guide after installing its Python requirements:

```bash
./.venv/bin/pip install -r k-fsw/docs/pdf/requirements.txt
./k-fsw/tools/docs/pdf.sh
```

Generated HTML is written to `build/docs/html/`; the printable guide is
written to `build/k-fsw-guide.pdf`. Physical HIL is selected explicitly and
requires local hardware.

See the [development guide](docs/development/index.md)
for contribution policy and coordinated west-pinned changes.
The [project status](docs/status/index.md)
separates implemented, software-tested, and physically verified scope.
