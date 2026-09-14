# `boton_test` hardware test

The `boton_test` module reads the NUCLEO USER button and drives three LEDs.
The unit tests and `tests/boton-test-smoke.sh` cover the logic. This script
checks the parts that need the board:

- pressing the button increases the counter, and nothing counts while the
  board is left alone;
- holding the button counts once; and
- the LEDs light from both the shell and the parameter table.

## Running it

From the workspace root, with the NUCLEO connected:

```bash
source .venv/bin/activate
export KFSW_DEBUG_SERIAL=/dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_<serial>-if02
./k-fsw/tests/hil/boton-test/button-acceptance.sh
```

Use `--no-flash` to test the image already on the board.

## Output

The script first waits with the board untouched, then prints one line for
each press with the host time, the counter and the module's `last_press_s`.
After that it switches the LEDs through both paths, reads them back with
`boton_test status` and compares the button parameters with the module status.

## Reading the timeline

The script asks for eight presses in four steps:

| Step | Expected lines |
| --- | --- |
| One press | one `+1` |
| Three presses a second apart | three `+1` lines, `DEVICE_S` increasing |
| One five-second hold | one `+1`, then nothing during the hold |
| Two fast presses | two `+1` lines with the same `DEVICE_S` |

Two counts for a hold, or any count while the board is untouched, is a
failure. The script prints the timeline but doesn't decide pass or fail.
