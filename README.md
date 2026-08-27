# K Flight Software — K-FSW

[![Software CI](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dgonzalez97/k-fsw/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-K--FSW_SDK-28a96b)](https://dgonzalez97.github.io/k-fsw/)

K-FSW is an open-source flight-software framework for satellites and satellite
subsystems. It runs on Zephyr RTOS and composes reusable platform, service, and
communications modules through a reproducible west workspace.

The [K-FSW SDK and Operator Manual](https://dgonzalez97.github.io/k-fsw/)
contains setup, architecture, command, testing, development, and public API
documentation.

## Architecture

`k-fsw` is the composition repository. Profiles select a board and
configuration without replacing the application or reusable modules.

```text
                    +----------------------+
operator / system ->| Zephyr shell         |
                    +----------+-----------+
                               |
                    +----------v-----------+
                    | K-FSW application    |
                    | lifecycle/composition|
                    +----+---------+-------+
                         |         |
              +----------v--+   +--v----------------+
              | platform    |   | services          |
              | time/storage|   | log/PARAM/FTP     |
              +-------------+   +---------+----------+
                                           |
                                +----------v----------+
                                | communications      |
                                | CSP + UART/KISS/RDP |
                                +---------------------+
```

KFSW-Linux is the same application built for Zephyr
`native_sim/native/64`. It is a first-class development and operations node,
not a separate POSIX implementation.

## Quick start

From a configured west workspace root:

```bash
west manifest --validate
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

Startup reaches the standard Zephyr prompt:

```text
@BOOT sw=kfsw-dev board=native_sim/native/64 ...
@READY uptime_ms=...

kfsw:~$ status
kfsw:~$ csp ping 2
kfsw:~$ param get test_u32
kfsw:~$ storage info
```

`kfsw` is the shell prompt identity, not a root command namespace.

## Supported profiles

| Profile | Target | Status |
| --- | --- | --- |
| `linux` | `native_sim/native/64` | Software verified |
| `linux_node2` | `native_sim/native/64` | Software integration peer |
| `linux_uart` | `native_sim/native/64` | Physical UART bridge profile |
| `nucleo_l496zg` | NUCLEO-L496ZG | Build and boot verified |
| `nucleo_l496zg_uart` | NUCLEO-L496ZG | Physical UART/KISS verified |

The current software includes the Zephyr shell, CSP, UART/KISS, RDP,
parameters and persistence, LittleFS storage, and FTP. Hosted CI does not need
physical hardware.

## Workspace repositories

| Repository | Responsibility |
| --- | --- |
| [`k-fsw`](https://github.com/dgonzalez97/k-fsw) | Composition, west manifest, profiles, tools, integration tests, and aggregate docs |
| [`kfsw-platform`](https://github.com/dgonzalez97/kfsw-platform) | Zephyr-backed platform capabilities |
| [`kfsw-services`](https://github.com/dgonzalez97/kfsw-services) | Reusable software services |
| [`kfsw-comms`](https://github.com/dgonzalez97/kfsw-comms) | CSP lifecycle, routing, and transports |
| [`kfsw-modules`](https://github.com/dgonzalez97/kfsw-modules) | Reusable spacecraft equipment and subsystem clients |

Exact dependency commits are pinned in [`west.yml`](west.yml). A dependency
commit must be published before hosted CI can reproduce a composition PR that
pins it.

## Local checks

Run the complete software-only CI sequence from the workspace root:

```bash
./k-fsw/tools/ci/all.sh
```

Individual entry points are available for the build matrix, quality, Twister,
integration, Valgrind, Robot, and Doxygen checks under `tools/ci/`. Physical HIL
is explicitly selected and never runs on a GitHub-hosted runner.

Build the documentation locally with:

```bash
./k-fsw/tools/docs/build.sh
./k-fsw/tools/docs/serve.sh
```

Generated HTML is written to `build/docs/html/` at the workspace root.

## Contributing

Development follows issue → branch → commits → pull request → CI → review →
merge commit → `main`. Use `feature/`, `fix/`, `test/`, `ci/`, or `docs/`
branches containing the GitHub issue number. See the
[development manual](https://dgonzalez97.github.io/k-fsw/development.html) for
the PR policy and multi-repository west workflow.

K-FSW is currently in an infrastructure-hardening phase. Keep changes focused
on their issue and do not add unplanned flight functionality.
