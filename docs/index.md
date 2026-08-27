# K-FSW SDK and Operator Manual

K Flight Software (K-FSW) is an open-source Zephyr flight-software framework
for satellites and satellite subsystems. This site is the reviewed source of
truth for building, operating, testing, and extending the current software.

K-FSW composes reusable platform, communications, and service repositories in
one west workspace. The same application runs as KFSW-Linux on Zephyr
`native_sim/native/64` and as an embedded image on NUCLEO-L496ZG.

## Start here

- @subpage getting_started "Getting Started" — prepare the west workspace,
  build a profile, run KFSW-Linux, or flash and debug the NUCLEO target.
- @subpage architecture "Architecture" — understand repository ownership,
  application composition, and exact dependency pins.
- @subpage communications "Communications" — operate CSP routing, UART/KISS,
  and the RDP-backed file-transfer path.
- @subpage services "Services" — use logging, parameters, persistent storage,
  and FTP.
- @subpage commands "Command Reference" — find compact shell syntax and
  operator examples.
- @subpage testing "Testing" — run local PR-equivalent checks, software Robot
  scenarios, or explicitly selected physical HIL.
- @subpage development "Development" — follow the issue-to-PR workflow and
  coordinate west-pinned changes across repositories.
- @subpage api_reference "API Reference" — browse public K-FSW C headers and
  conceptual API groups.

## Current supported targets

| Profile | Target | Purpose |
| --- | --- | --- |
| `linux` | `native_sim/native/64` | Primary KFSW-Linux console and CSP node 1 |
| `linux_node2` | `native_sim/native/64` | Second node used by software integration tests |
| `linux_uart` | `native_sim/native/64` | Linux side of the physical UART/KISS bridge |
| `nucleo_l496zg` | `nucleo_l496zg` | Base embedded image |
| `nucleo_l496zg_uart` | `nucleo_l496zg` | Embedded image with CSP KISS on USART3 |

## Verified software boundary

The current baseline includes the Zephyr shell, CSP, UART/KISS, RDP, PARAM,
parameter persistence, LittleFS storage, FTP, ztest/Twister, Valgrind, and
Robot Framework. Hosted CI uses simulation only. Physical ST-LINK, FTDI, serial
devices, and a NUCLEO board remain explicit local HIL dependencies.

The prompt identity is `kfsw:~$`; `kfsw` is not a root command namespace.

```text
kfsw:~$ status
kfsw:~$ csp ping 2
kfsw:~$ param get test_u32
kfsw:~$ storage info
kfsw:~$ ftp 2 ls /
```

## Documentation ownership

Human-maintained Markdown lives beside the composition and integration code in
`k-fsw/docs/`. Public API comments live in the owning repositories under
`include/kfsw/`. The aggregate Doxygen build reads only those project-owned
inputs and publishes generated HTML; generated files are never committed.
