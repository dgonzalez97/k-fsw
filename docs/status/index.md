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
| Watchdog and health | Deliberate starvation reset; healthy and overdue-component runs | STM32 watchdog cannot be disarmed once started |
| Persistence | Parameter snapshots preserved across MCUboot swaps | Configuration migration needs release-specific checks |
| Button and LEDs | Debounce, press counts, and observed LED operation | Optional profile |
| FRDM-K64F / Pico W | Boot and shell commands | Services disabled in bring-up profiles |

## Current limits

- `@READY` marks completed startup. `@SERVICES` reports startup failures;
  runtime liveness is handled by health monitoring.
- Routes are fixed once the CSP router starts. There is no automatic failover.
- Optional radio encryption authenticates packets and rejects wire replays.
  Other links need their own access policy. Commands have no request deduplication;
  check the outcome before resubmitting a command whose reply was lost.
- Events and retained reset notes are held in RAM.
- HK skips elapsed schedule slots after a slow collection. Remote reads have
  a shared budget; local callbacks and drivers need their own time bounds.
- A 120-second loaded CAN run measured a 107.124 ms maximum HK collection
  and 127 us maximum router wake-to-run delay. PARAM and temperature workers
  left 2,208 and 1,184 stack bytes unused. These are observed maxima for
  `fwu-can-probes`, with radio encryption disabled.
- The longer soak stopped after a console command was misread. The revised
  fixture keeps console writes outside flash traffic; its full rerun and
  interrupted-update cases remain pending. Each flight composition needs
  its own timing and endurance measurements.

Use repository issues for planned work. Keep this page to supported behaviour
and measured limits.
