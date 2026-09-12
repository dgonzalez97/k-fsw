# Testing {#testing}

Test commands and bench procedures live with the tests:

| Task | Entry point |
| --- | --- |
| Unit and integration tests | [Test guide](https://github.com/dgonzalez97/k-fsw/blob/main/tests/README.md) |
| Software checks | [CI scripts](https://github.com/dgonzalez97/k-fsw/tree/main/tools/ci) |
| Hardware tests | [HIL suites](https://github.com/dgonzalez97/k-fsw/tree/main/tests/hil) |
| Recorded coverage and limits | @ref project_status |

For changes to a service, run its existing unit suite and the integration
case that uses it. Hardware changes also need the matching board and bench.
Record the image commit and profile with the result.

See @ref development for the contribution workflow.
