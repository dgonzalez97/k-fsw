# UHF radio HIL

Hardware tests for UHF radios, one directory per radio.

```text
radio-uhf/
`-- holybro/
    |-- raw byte-link smoke test
    `-- CSP/KISS link smoke test
```

The CSP and KISS code is in `kfsw-comms`; these scripts only set up the radio
benches.
