# Using health {#health-guide}

Health watches the components that have to keep running, and it is the only
thing that feeds the hardware watchdog. If a component stops reporting inside
its deadline, health stops feeding, and the watchdog resets the node.

Health answers "is this component still reporting on time", not "is this unit
fit to use". It does not switch to a spare or degrade instead of resetting;
`limits` and the command registry do that, as the last section shows.

![What takes part](media/health-shape.svg)

## Where it runs

`CONFIG_KFSW_HEALTH` depends on `CONFIG_KFSW_WATCHDOG`. The NUCLEO L496 and
Pico W images carry it, both checking every 500 ms. The hosted `linux` image
has no hardware watchdog and no `health` command; the unit suites switch the
service on explicitly on `native_sim`.

| Option | Default | Effect |
| --- | --- | --- |
| `CONFIG_KFSW_HEALTH` | off | The service |
| `CONFIG_KFSW_HEALTH_MAX_COMPONENTS` | 8 (1 to 32) | Components that may register |
| `CONFIG_KFSW_HEALTH_INTERVAL_MS` | 1000 (50 to 60000) | How often deadlines are checked |
| `CONFIG_KFSW_HEALTH_SLOTS` | on in the application | Publish each named component in table 31 |
| `CONFIG_KFSW_HEALTH_SLOTS_HEADER` | `kfsw/app/health_slots.h` | The header naming this composition's slots |

`health_interval_ms` changes the check interval at run time and refuses a
value slower than the watchdog feed interval.

## To have a component watched

1. Register the component with `kfsw_health_register()`, giving a name of 15
   characters or fewer and the deadline in milliseconds.
2. Call `kfsw_health_report(handle)` from the component's own loop, inside
   every deadline.
3. Add the name to `app/include/kfsw/app/health_slots.h` with an index from 0
   to 13 that no other slot has used.
4. Run `tools/ci/quality.sh` and confirm the `HEALTH SLOTS` line names the
   component.
5. On the target, confirm `health list` shows the component and
   `param get health_<name>_state` reads 0.

```c
static uint8_t handle;

int start(void)
{
	int result = kfsw_health_register("myservice", 5000U, &handle);
	if (result != 0) {
		return result;
	}
	/* then, from the component's own loop, inside every 5000 ms: */
	return kfsw_health_report(handle);
}
```

A component that registers and never reports resets the node, so register
only once the component can report.

```c
/* app/include/kfsw/app/health_slots.h */
#define KFSW_HEALTH_SLOTS(SLOT)                                                \
	SLOT(app, 0)                                                           \
	SLOT(csp, 1)                                                           \
	SLOT(kiss, 2)                                                          \
	SLOT(can, 3)                                                           \
	SLOT(myservice, 4)
```

The index fixes the slot's offsets on the wire, so it is never reused or
renumbered. Removing a component leaves its index unused. Table 31 holds at
most 14 slots.

Nothing in C ties the registered name to the slot name.
`tools/ci/health-slots.py`, run by `quality.sh`, reads the
`kfsw_health_register()` calls under `app/src`, except the shell's, and refuses
a slot nothing registers, a registration no slot names, and an index given
twice.

## Reading it

From the shell:

```text
health status            whether the watchdog is being fed, and why
health list              each watched component and its deadline
health watch <name> <ms> watch a component nothing reports, for testing
health report <handle>   report one alive by hand
```

Table 31 carries the service as a whole:

| Parameter | Meaning |
| --- | --- |
| `health_state` | 0 ok, 1 faulted, 2 stopped |
| `health_feeding` | Whether health is the one feeding the watchdog |
| `health_faults` | Times the node went from ok to faulted since boot |
| `health_feeds` | Watchdog feeds issued since boot; stops at 4294967295 |
| `health_faulted_by` | The component holding the fault, or empty |
| `health_components` | Components currently watched |
| `health_interval_ms` | Check interval; writable, refused above the feed interval |

and two parameters for each slot the composition names:

| Parameter | Meaning |
| --- | --- |
| `health_<slot>_state` | 0 ok, 1 overdue, 2 absent |
| `health_<slot>_since_ms` | Milliseconds since its last report; `4294967295` when absent |

`absent` means no component of that name is registered: on a build expected
to carry it, the registration is missing or misspelled. Ok is zero for both the
service and a slot, so `> 0` is the comparison for a `limits` rule.

## What happens when a component goes overdue

![One component stops reporting](media/health-overdue.svg)

At its next check, health takes the first component past its deadline, sets
`health_state` to faulted, names it in `health_faulted_by` and stops feeding
the watchdog. It logs the fault once, emits `KFSW_EVENT_HEALTH_FAULT` with the
name as payload, and writes a last-words note when `CONFIG_KFSW_LASTWORDS` is
on.

Health does not restart the component. If the component reports again before
the watchdog expires, the next check feeds again and withdraws the note.
Otherwise the watchdog resets the node, and the next `@BOOT` line carries
`reset_cause=watchdog`.

One late component resets the whole node, so set each deadline to survive the
component's worst cycle. The margin is the watchdog timeout, 8000 ms on the
NUCLEO, less the time since the last feed.

## Reacting before the reset

A `limits` rule can watch a slot and release a command before the watchdog
expires. Rules live in a text file in the `limits` directory of the FTP root,
`limits.txt` by default: five whitespace-separated fields, then the action.
The action is a command from the command registry with its arguments, not a
shell command; a file naming an unknown command is refused whole.

```text
# parameter            cmp  bound  dwell  node  action
health_csp_state       >    0      2      0     event_stats
health_app_since_ms    >    3000   1      0     event_tail 0
```

`dwell` is how many consecutive sweeps the comparison must hold. A rule fires
once per crossing and re-arms when the comparison is false again. The `node`
column is where the action is released.

Limits sweeps every 10000 ms by default, longer than the 8000 ms watchdog
timeout on the NUCLEO. For a rule on a health slot to fire before the reset,
set `limits_interval_ms` so that dwell times the interval is well below the
watchdog timeout.

## Verification

The unit suites cover the service and the slots on `native_sim`:

```bash
./k-fsw/tools/ci/unit.sh --test kfsw.services.health
./k-fsw/tools/ci/unit.sh --test kfsw.services.health.parameters
```

The second covers the slots: ok, overdue and absent states, the age
advancing, slots independent of registration order, and fixed offsets.

On hardware, with a NUCLEO L496 on its ST-LINK:

```bash
KFSW_DEBUG_SERIAL=/dev/serial/by-id/usb-...-if02 \
  ./k-fsw/tests/hil/health/health-reset.sh
```

The script checks that the board stays up past three watchdog timeouts while
healthy, both slots read ok and their age changes, a component that stops
reporting resets the board, the next boot names the watchdog, and the slots
read ok afterwards. It also checks that a component registered at run time
creates no parameter, since a table that grew would move the wire offsets.

## Quiet links and stalled receivers

The `csp` component still probes this node through the router. Two more fixed
slots cover opened receivers when `CONFIG_KFSW_CSP_LIVENESS=y`: `kiss` at slot
2 for polling UART reception, and `can` at slot 3 for CAN. Each uses
`CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS`, normally 8000 ms. Crypto is optional.

The libcsp RX threads count every pass, including idle passes. `comms uart
info` and `comms can info` print `rx_passes`. The wrapping count advances on a
quiet link and freezes when the receiver stalls. Packet counters alone cannot
make that distinction. The liveness work item reports `kiss` only when every
watched polling receiver advances, and reports `can` independently. A receiver
missing from a later snapshot remains an obligation. A receiver that never
opened is a startup failure and is not registered; UART's `receiver_opened`
signal is separate from `interface_started`, which can survive a failed open.

Quiet links keep feeding indefinitely, including between ground passes. Ground
contact expiry remains the separate 24 h ground watchdog. CAN bus-off alone
is not a health fault: an external bus fault is not repaired by resetting the
node, and resets consume boot attempts. Starvation that stops a receiver from
running does count as a stall.

The receiver sampling delay is the configured liveness period plus the
self-ping wait and system workqueue scheduling delay. Health then evaluates
its own deadlines on `health_interval_ms`. Feed suppression occurs after the
last progress report exceeds its deadline, within one further health check
interval under normal scheduling. This is a deadline plus one health sampling
period from the last report, not a strict deadline from the suspend command.
A final sample can still report progress made immediately before suspension.
The shared system workqueue prevents an unconditional real-time bound.

Interrupt-driven KISS on NUCLEO and Pico has no polling loop and no registered
`kiss` component: its table slot reads absent. A silently disabled RX interrupt
on a quiet link is an accepted coverage gap. The `app` component, router
self-ping and 24 h ground watchdog provide the remaining coverage. The UART
codec worker is also outside this supervision; successful receiver open does
not prove that worker is healthy. Golden leaves CSP liveness off and retains
its ground watchdog. Default Linux still leaves health and watchdog off.

### Software fault injection

Run `tests/kiss-rx-stall-smoke.sh` for a dedicated Linux diagnostics image with
a counter watchdog, or `tests/can-rx-stall-smoke.sh` for vcan0 and ground node
17. The latter reports NOT RUN when vcan0 is unavailable. Both leave the link
quiet for more than three deadlines, check increasing feeds, discover the RX
thread ID, suspend it, and check frozen progress and feeds with `faulted_by`
naming the link. They report hosted reset evidence separately. A hosted result
is software evidence only.

### Hardware procedure

HIL: NOT RUN until a human observes and records this procedure.

Build NUCLEO-L496ZG with `config/profiles/nucleo-can.conf` and its overlay,
plus `tests/hil/fwu/flight-diagnostics.conf`, `CONFIG_KERNEL_SHELL=y`,
`CONFIG_THREAD_MONITOR=y`, `CONFIG_THREAD_NAME=y`, `CONFIG_INIT_STACKS=y` and
`CONFIG_THREAD_STACK_INFO=y`. Thread suspend/resume commands are selected by
this diagnostics configuration. It differs from the flight image.

Use the human-confirmed ST-LINK `/dev/serial/by-id/...` console. CAN requires
an external transceiver on PD0/PD1, termination and common ground, with PCAN
`can0` at 500 kbit/s. KISS uses USART3 PD8 TX/PD9 RX, crossed FTDI wiring,
common ground, 3.3 V logic and 115200 baud. Board node is 2, CAN peer 17,
and KISS peer 1.

1. Record image and dependency SHAs, device IDs, effective routes on both ends,
   interfaces and the initial boot-budget state.
2. Leave both links quiet for at least 30 seconds and more than three
   deadlines. Confirm `state: ok`, `feeding: yes`, increasing `feeds`, no reset.
3. Ping both ways on CAN and KISS. Record `tx/rx/txerr/rxerr/drop/frame`.
4. Run `kernel thread list`, find `csp_rx_CAN`, and copy its hexadecimal ID.
   Run `kernel thread suspend <hex-id>` once. Names are not accepted as IDs.
5. Record frozen CAN `rx_passes`, `faulted_by: can`, `feeding: no`, stopped
   feeds, IWDG reset, the second `@BOOT`, reset cause and recovery.
6. Record actual boot-budget accounting. Repeat only with owner agreement,
   because resets can exhaust the flight boot budget and enter Golden.
7. Confirm the interrupt-driven `kiss` component is absent, including
   `health_kiss_state=2` in table 31.
