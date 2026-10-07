# Procedures

A procedure is a text file of commands that `fbo run <name>` carries out one
line at a time. On a node it lives in `/procedures` under the FTP root, so it
gets there with `ftp put <node> <local> /procedures/<name>`.

`examples/` holds procedures meant to be copied:

| File | What it does |
| --- | --- |
| `check-in.txt` | Asks the node how it is and what it recorded, stopping at the first failure |
| `uptime-report.txt` | Starts a housekeeping report that collects the uptime once a minute |

`tests/fbo-smoke.sh` runs every example, so one that stops working fails CI.

`smoke.txt` and `utc-wait.txt` are test fixtures: they exercise each kind of
line and the UTC wait, failures included, and are not examples.
