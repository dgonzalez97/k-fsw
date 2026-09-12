# Project status {#project_status}

[TOC]

## Recorded status

This page records implementation limits and software and bench evidence.
Image revisions identify the recorded runs where available. These results
do not imply that later revisions passed the same tests.

Linux and NUCLEO-L496ZG are the reference targets. FRDM-K64F and Pico W
have shell bring-up evidence. No target is flight-qualified.

## Capability matrix

Software and physical results below are recorded evidence. Optional profiles
are listed in @ref targets; test procedures are in @ref testing.

| Area | Software checks | Recorded physical checks | Limits |
| --- | --- | --- | --- |
| Time and reset cause | Time ztest; native boot/shell tests | NUCLEO boot/reset readback | Clock accuracy and correlation not qualified |
| Logging | Filtering and shell diagnostics | NUCLEO console output | Console only; no persistent log or rate limiting |
| CSP routing | Route validation, precedence, two- and three-node tests | Bidirectional NUCLEO UART and Holybro ping | Static routes; no automatic failover |
| UART/KISS | Independent interfaces, direct routes, transit | USART3/FTDI and one Holybro link | Multiple links tested in software only |
| CAN | NUCLEO and ground compositions build | `54ac87f`: ping, identity, parameters; RX 1258 / TX 84; zero bus errors | External transceiver and termination required; Yamcs bridge uses KISS |
| Ground nodes | Nodes 16 and 19 identify and ping | Local profiles need no hardware | One direct peer link per launcher; no central orchestration |
| UHF module | Identity/status tests; ground and NUCLEO builds | Uses the verified Holybro fixture | No live modem readback or AT control |
| Holybro fixture | Script validation and negative paths | Raw 100/100; bidirectional CSP; remote PARAM; 256-byte file round trip with matching CRC and clean counters | No RF performance or soak qualification |
| Button/LED example | Debounce, state, PARAM, GPIO, Linux smoke | untouched count stayed zero for 10 s; presses reached 7; LEDs observed through shell and PARAM | Individual press/hold attribution pending; opt-in table 67 |
| Firmware update | 52 cases across update and block protocol; 20000-byte CSP transfer | `db64963`: `fwu-before` changed to `fwu-after` over Holybro; link recovered; image confirmed | One upload; sequential blocks; signature checked by bootloader |
| MCUboot | Sysbuild and signature checks | `4dfd520`: revert, confirmation across two reboots, wrong-key rejection, stored value preserved | Golden region reserved; recovery-image selection absent |
| Health | 14 native cases for registration, deadlines, recovery | `f9973be`: healthy for three watchdog periods; overdue component caused reset | Liveness checks; application thread supervised |
| Watchdog | Six native cases for intervals, reset decoding, missing device | `25f03f9`: fed for 18 s; deliberate starvation reset within 13 s; watchdog cause read back | One channel; STM32 independent watchdog cannot be disarmed |
| Parameters | Scalar, string, array, validation, write-mode, and table tests | `43ac957`: NUCLEO table listing, remote table reads and writes over Holybro | Available tables depend on the composition |
| Remote PARAM | Native list/get/set and errors | valid log level 3; invalid 5 restored compiled default 1 | Fixed descriptor pool; persistence operations are local |
| Persistence | Unit, cross-process, corrupt-snapshot, Valgrind checks | MCUboot bench preserved stored values across swaps | No complete NUCLEO persistence acceptance matrix or migration framework |
| Storage | Mount, capacity, cross-process persistence | NUCLEO `storage info` and `storage test` | One 64 KiB volume in reference profiles |
| Files | Codec, sandbox, native transfers to 8 KiB, ground round trips | 4 KiB and 16 KiB UART transfers; 256-byte Holybro round trip | One server worker/client workspace; PUT/GET need two nodes |
| Self-addressing | Local ping and command integration | Node 16 and NUCLEO node 2 over the Holybro bench | Uses the node address on the libcsp loopback interface |
| Commands | Registry, validation, unknown requests, local/remote integration | Holybro dispatch, self-addressed command, unknown-command rejection | Synchronous; no authentication or duplicate suppression |
| Retained reset note | Six cases for validation and reporting | commanded reboot note read back; power cycle cleared it; watchdog starvation reason read remotely | Volatile RAM; voltage-dip trigger not observed |
| Events | Ring wrap, counters, visitor and error tests | Remote NUCLEO counters and decoded record over Holybro | RAM only; no persistent journal |
| Housekeeping | Collection, CSP, and Yamcs frame checks | `54ac87f`: temperature samples and archive, detailed below | Results apply to the named report and bench |
| Linux | Hosted builds, units, integration, memory checks, Robot | Not applicable | Simulation does not establish MCU timing or electrical behaviour |
| NUCLEO-L496ZG | Hosted reference build | Boot, shell, storage, links, and service runs listed above | Optional profiles need separate acceptance; no flight qualification |
| FRDM-K64F | Build profile | Prompt, status, version, help | Shell bring-up; services disabled |
| Pico W | Build profile | USB shell, status, version, help | Shell bring-up; Wi-Fi and services disabled |

## Known limits

- `@READY` marks completed startup. It does not aggregate service health.
- Routes are static; automatic link failover and dynamic routing are absent.
- CRC32 detects corruption. Command authentication and encryption are absent.
- Events are held in RAM and lost on reset.
- Parameter migration is limited to snapshot compatibility checks.
- Physical HIL is manually invoked. A new board, radio, or profile needs its
  own acceptance run.
- Timing, stack usage, endurance, long-duration soak, and flight qualification
  remain separate work.

The `boton_test` bench recorded press counts and LED operation. Individual
press/hold attribution remains pending.

## Housekeeping bench

Recorded against `54ac87f`: NUCLEO-L496ZG, a Holybro SiK pair at 433 MHz,
and Yamcs 5.13.0.

- Five-value report, 2000 ms period, 20 collections without missing values.
- Five consecutive samples retrieved over the radio; temperature matched the
  console reading.
- Yamcs received 63 valid datagrams and archived 56 samples with distinct
  generation times.

| Read | Time for five values |
| --- | --- |
| Individual parameters, first read | 9.21 s |
| Individual parameters, descriptors cached | 1.17 s |
| One housekeeping exchange | 0.28 s |

These timings apply to this bench and report. Beacon results are separate;
see @ref ground. The frame comparison is in `tests/hk-yamcs-smoke.sh`.

## Next work

- Automate the existing HIL procedures with hardware locking and recovery.
- Extend fault injection, stack/timing measurements, and soak tests.
- Complete outstanding physical acceptance for optional profiles.

Use repository issues for the current work queue.

## Keeping this page current

When a feature merges:

1. verify the final manifest and target defaults;
2. identify the software and physical tests that actually ran;
3. update the relevant explanatory chapter;
4. adjust one status row without turning the manual into an issue mirror; and
5. keep future work separate from current behavior.

Keep recorded results tied to their image, configuration, and bench.
