# Shared 64-bit state audit

This audit covers `kfsw-services` and `kfsw-platform`. On 32-bit targets an
unprotected 64-bit copy can combine halves from different updates. The table
records the shared state and the contexts that access it. Paths are relative
to the named repository; line numbers identify declarations or access sites.

| Repository and file:line | State | Writer context | Reader context | Consequence and protection |
| --- | --- | --- | --- | --- |
| `kfsw-services/src/health/health.c:25` | `last_report_ms` | Component caller through register/report; supervision start | Health workqueue or explicit evaluate; component status callers | A tear can corrupt reset diagnostics. Evaluation previously reread the timestamp after unlocking for lastwords. The fix uses the elapsed value computed under `health_lock` and copies the fault name there too. All remaining timestamp accesses hold that mutex. |
| `kfsw-platform/src/watchdog.c:27` | `watchdog_last_feed_ms` | Keepalive workqueue or feed caller | Status caller | Operator feed age; all shared accesses hold `watchdog_lock`, including the copy for status. |
| `kfsw-services/src/gndwdt/gndwdt.c:32` | `last_contact_ms` | Start/contact/evaluate callers | Ground watchdog workqueue and status callers | Reset deadline and operator age; `gndwdt_lock` covers accesses. |
| `kfsw-services/src/log_history.c:20` and `src/log_history_internal.h:22` | Record sequence/uptime and header `next_sequence` | Logging callers, including ISR; retained restore | History/window callers | Operator ordering and retention checks; `history_lock` covers writes and snapshot copies. Formatting uses the private copy. |
| `kfsw-services/src/event/event.c:78` | Ring record `monotonic_us` | Event emit callers, including ISR | Event get/visitor and journal submission | Operator timestamp; `ring_lock` covers writes and copies. Visitors and journal receive private copies. |
| `kfsw-services/src/journal/journal.c:20` and `src/journal/journal_store.h:11` | Boot, store sequences/next sequence/highest boot, pending record sequence/boot/UTC | Journal writer or explicit flush/start caller | Journal get/status callers | Persistent ordering and operator UTC; `journal_lock` covers the store and pending record. Queue handoff copies event records through Zephyr's message queue. |
| `kfsw-services/src/hk/hk.c:23` and `:37`, `src/hk/hk_internal.h:35` | Collection order, generations, schedule revisions and per-sample collection order | Collection/configuration callers | Collection completion and class-selection callers | Stale collection rejection and sample order; `hk_lock` covers accesses, with private generation snapshots used across collection. |
| `kfsw-services/src/hk/hk.c:27` | `config_revision` | Configuration and restore callers | Persistence snapshot/result callers | Dirty-settings decision; `hk_lock` covers writes and snapshot reads. `kfsw_hk_config_revision()` requires the caller to hold it; persistence does. |
| `kfsw-services/src/hk/hk_internal.h:32` | Report `next_uptime_ms` | Report define/period/restore and schedule finish callers | HK scheduling thread | Collection timing; `hk_lock` covers state and due snapshots. The due revision/uptime fields at lines 47–48 are caller-owned copies. |
| `kfsw-services/src/hk/hk_beacon.c:20` | Beacon `next_uptime_ms` | Configuration and beacon tick callers | HK scheduling thread | Beacon timing; `hk_lock` covers writes and reads, including the caller of `kfsw_hk_beacon_wait()`. |
| `kfsw-services/src/fbo/fbo_schedule.c:39`, `include/kfsw/services/fbo.h:183` | Relative due uptime, absolute due/released UTC | Add callers and scheduler thread | Queue get/status and scheduler thread | Command release decisions and operator timestamps; `queue_lock` covers slot state and copies. Invocation uses a private entry snapshot. |
| `kfsw-services/src/fbo/fbo_schedule.c:581` | `last_utc`, `last_mono` | Scheduler thread | Same scheduler thread | Clock-step detection; no second context accesses these function-static values. |
| `kfsw-services/src/fbo-parameters/fbo_table.c:33` | `sched_next_due_utc` | Parameter sampling callers | Parameter read/snapshot callers | Operator UTC; parameter table mutex serializes sampling and copying. Schedule status is copied under its own queue mutex first. |
| `kfsw-services/src/command/command_retry.h:7` | Retry nonce, ticket, expiry | Command server thread | Same command server thread | Duplicate-command/expiry decisions; retry cache is private to that thread. Client token/deadline values are private to the invocation, serialized by `command_client_lock`. |
| `kfsw-services/include/kfsw/services/parameter.h:43` | U64/I64 parameter storage | Parameter set/default/sample callers | Parameter get/snapshot/CSP/persistence callers | Operator values and decisions; `kfsw_param_lock` serializes service access. External producers must supply their own synchronized sample callback. |
| `kfsw-services/src/limits/limits.c:64` and `include/kfsw/services/limits.h:101` | Rule bounds and sampled parameter unions | Rule loading/evaluation callers | Evaluation and rule visitor callers | Limit decisions; `rules_lock` serializes rules and evaluation scratch. Widened U64/I64 comparison operands are local values. |
| `kfsw-platform/src/time.c:7` | Monotonic return values | Zephyr timer | Calling thread or ISR | No module-owned shared 64-bit variable. `k_uptime_get()`/`k_uptime_ticks()` read through Zephyr's timeout spinlock. The Cortex-M SysTick 64-bit cycle accessor uses its driver spinlock. |
| `kfsw-platform/src/storage.c:232`, `src/wallclock.c:28` | Capacity/free-byte and wallclock results | Calling thread via filesystem/RTC APIs | Same caller | 64-bit arithmetic and output copies use caller-owned storage; no cached shared 64-bit value in these modules. |

The scan also covered local deadlines and wire encode/decode values in
parameter CSP, remote log, remote execution, FBO parsing/waiting, HK storage
and remote collection, FTP space checks, and FWU file seeking/client reads.
Those values are local arithmetic or caller-owned records. HK wire sample
seconds are `uint32_t`. FTP offsets, sizes and result byte counts are
`uint32_t`, and aggregate byte totals use `atomic_t`. FWU progress and image
sizes are `uint32_t` under `fwu_lock`. No width reduction or new locking
framework is needed.

## Regression coverage

`tests/unit/services_health_fault` selects both `native_sim/native` and
`native_sim/native/64`. It advances a fake monotonic clock across the low-word
rollover, then uses the fault log call to let another thread report and replace
the overdue component before lastwords and the event are emitted. The note
must retain the original elapsed time and the event must retain the original
name. The test also checks healthy feeding and recovery.

The watchdog release/feed functions are substituted: this is software
concurrency evidence, not watchdog hardware acceptance. The test controls the
interleaving rather than relying on a probabilistic torn load.
