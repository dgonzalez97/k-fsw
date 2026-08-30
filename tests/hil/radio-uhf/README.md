# UHF radio HIL

This directory groups physical tests by radio category. Device-specific
fixtures live below the category rather than defining the whole communications
architecture.

Current structure:

```text
radio-uhf/
└── holybro/
    ├── raw byte-link smoke test
    └── CSP/KISS link smoke test
```

Reusable CSP and KISS behavior remains owned by `kfsw-comms`. These fixtures
only adapt that behavior to physical serial/radio benches.
