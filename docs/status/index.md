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
- Commands have no authentication or duplicate suppression. CRC32 detects
  corruption; it does not authenticate a sender.
- Events and retained reset notes are held in RAM.
- HK skips elapsed schedule slots after a slow collection. Remote reads have
  a shared budget; local callbacks and drivers need their own time bounds.
- The CAN bench image left 2,208 bytes unused on the PARAM worker stack and
  1,184 on the temperature worker. These are observed high-water marks.
  Worst-case latency, flash endurance, and long soak runs need measurements
  for each flight composition.

Use repository issues for planned work. Keep this page to supported behaviour
and measured limits.
