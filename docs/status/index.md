# Project status {#project_status}

Linux and NUCLEO-L496ZG are the reference targets. FRDM-K64F and Pico W
have shell bring-up support.

## Recorded status

Bench results apply to the recorded image and configuration. Procedures live
under `tests/hil/`; use @ref testing to find the relevant fixture.

| Area | Recorded coverage | Limits |
| --- | --- | --- |
| CAN | `54ac87f`: NUCLEO ping, identity, and parameters | External transceiver and termination required |
| Holybro | Bidirectional CSP, remote parameters, file round trips | No RF range or endurance qualification |
| Radio encryption | Native peers: AES-256-GCM, key changes, plaintext refusal, replay rejection | Encrypted Holybro acceptance pending |
| Firmware update | `db64963`: radio upload, boot, and confirmation | Signature checked by MCUboot; golden-image selection absent |
| CAN firmware update | `fwu-can-checked`: FTP and FWU lite uploads, both slot readbacks, rollback, confirmation, and PARAM reads | NUCLEO at 500 kbit/s; bench image and configuration recorded by the fixture |
| Housekeeping | `54ac87f`: collection, radio retrieval, and Yamcs archive | Measurements cover one report and bench |
| Watchdog and health | Reset after starving the watchdog; runs with healthy and overdue components | STM32 watchdog cannot be disarmed once started |
| Persistence | Parameter snapshots preserved across MCUboot swaps | Configuration migration needs release-specific checks |
| Button and LEDs | Debounce, press counts, and observed LED operation | Optional profile |
| FRDM-K64F / Pico W | Boot and shell commands | Services disabled in bring-up profiles |

## Flash and RAM budget

Measured on `nucleo_l496zg`, the reference MCU composition, with
`size -A build/nucleo_l496zg/zephyr/zephyr.elf` for the sections and
`readelf -lW` for what is actually loaded. Record these again after any change
that is meant to move them.

| Section | Bytes |
| --- | --- |
| `text` | 147,488 |
| `rodata` | 53,024 |
| `data` | 1,139 |
| `bss` | 47,203 |
| `noinit` | 107,208 |

That is 206,452 bytes of the 983,040-byte flash region, and 155,550 bytes of
the 327,680-byte RAM.

The September size work took `text` down to 138,628 bytes; composing the
diagnostics layer - last words, the retained log ring, the hardware watchdog and
health - spent 8,860 of that back, which is the trade the budget exists to make
visible.

Levers, each measured on its own against that image:

| Lever | Bytes of `text` | State |
| --- | --- | --- |
| Format the UTC clock directly instead of through `strftime` | 6,528 | taken |
| `CONFIG_KFSW_PARAM_FLOAT=n`, which drops float `printf` and the shell's own conversions | 6,688 | taken on both reference profiles; a table that declares a float is refused while it is off |
| `CONFIG_ASSERT_VERBOSE=n` | 7,748 | available; kept on while bench work is ahead, because it drops the file and line behind every assertion |
| `CONFIG_LTO=y` | not measurable | unavailable: it needs `ISR_TABLES_LOCAL_DECLARATION`, which conflicts with shared interrupts in this composition |
| Log identifiers instead of format strings | 4,160 | design change; also reduces downlink bytes now that logs are read remotely |

## Current limits

- `@READY` marks completed startup. `@SERVICES` reports startup failures;
  runtime liveness is handled by health monitoring.
- Routes are fixed once the CSP router starts. There is no automatic failover.
- Optional radio encryption authenticates packets and rejects wire replays.
  Other links need their own access policy. Legacy commands can run twice if
  resent. `cmd retry` suppresses duplicates within one invocation; starting it
  again is a new operation.
- The ordinary event ring and retained reset notes are held in RAM. The
  optional persistent journal stores boot reports and selected important
  events; queued records can be lost on power failure. Native restart and
  storage-fault tests cover it; physical power-cut qualification is pending.
- HK skips elapsed schedule slots after a slow collection. Remote reads have
  a shared budget; local callbacks and drivers need their own time bounds.
- A 120-second loaded CAN run measured a 107.124 ms maximum HK collection
  and 127 us maximum router wake-to-run delay. PARAM and temperature workers
  left 2,208 and 1,184 stack bytes unused. These are observed maxima for
  `fwu-can-probes`, with radio encryption disabled.
- The longer soak stopped after a console command was misread. The revised
  fixture keeps console writes outside flash traffic; its full rerun and
  interrupted-update cases are still to be run.
