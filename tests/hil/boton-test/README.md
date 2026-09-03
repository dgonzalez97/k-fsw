# `boton_test` hardware acceptance

The `boton_test` reference module owns a debounced USER button and three
developer LEDs. Its state machine, debounce interval and parameter exposure are
covered by unit tests and by `tests/boton-test-smoke.sh`, but three claims can
only be settled with the board in front of you:

- a physical press increments the counter, and an untouched board does not;
- a held button counts once on the edge rather than repeating while down; and
- the LEDs physically light, through both the shell and the parameter table.

## Running it

From the workspace root, with the NUCLEO connected:

```bash
source .venv/bin/activate
export KFSW_DEBUG_SERIAL=/dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_<serial>-if02
./k-fsw/tests/hil/boton-test/button-acceptance.sh
```

Add `--no-flash` to use the image already on the board, which is worth doing
when an operator needs to be told to start pressing at a known moment instead
of guessing when the build and flash have finished.

## What it records

A baseline period with the board untouched, then a timeline with one line per
press the module reports, carrying the host time, the counter and the module's
own `last_press_s`. The gestures are read back out of that timeline rather than
the fixture trying to keep step with the operator.

Both LED paths are then exercised in turn and read back through
`boton_test status`, and PARAM IDs 6 and 7 are compared against the module's
own status so the parameter exposure is checked against the same hardware.

## Reading the timeline

Eight presses are asked for, in four gestures:

| Gesture | Expected in the timeline |
| --- | --- |
| One press | a single `+1` |
| Three presses a second apart | three `+1` lines, `DEVICE_S` advancing |
| One five-second hold | a single `+1`, then nothing for the hold |
| Two fast presses | two `+1` lines within the same `DEVICE_S` |

A hold that produces two counts, or a rest period that produces any, is a
finding. The fixture prints the timeline; it does not grade it.
