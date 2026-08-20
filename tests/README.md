# K-FSW Tests

Host and HIL test runners. Developer build, run, flash, debug, and serial
utilities remain in `tools/`.

Run test scripts from the west workspace root, for example:

```bash
./k-fsw/tests/shell-smoke.sh
./k-fsw/tests/csp-smoke.sh
```

Current:

- native K-FSW shell smoke test
- two-node native CSP/KISS integration test
- NUCLEO boot/readiness HIL smoke test
- physical FTDI-to-NUCLEO CSP UART HIL test

Planned:

- fake clocks
- fake devices
- SocketCAN/vcan
- ZMQ simulation
- fault injection
- telemetry inspection
- soak tests
