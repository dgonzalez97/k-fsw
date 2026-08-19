# K Flight Software — K-FSW

K-FSW is an open-source flight software framework for satellites, built
around Zephyr RTOS.

This composition repository owns the application, west manifest, board
configuration, developer tools, test support, CI.

Supported targets:

- `nucleo_l496zg`: STM32 NUCLEO-L496ZG
- `linux`: Zephyr `native_sim/native/64`

Both targets run the same K-FSW application and services on Zephyr RTOS
4.4.0.

Build and run KFSW-Linux:

```bash
./k-fsw/tools/build.sh linux
./k-fsw/tools/run-linux.sh
```
