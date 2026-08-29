# Command Reference {#commands}

Commands are registered at the Zephyr shell root. In examples, `kfsw:~$` is
the prompt; `kfsw` is not a command or namespace.

Availability follows the selected Kconfig configuration. Use `<node>` for a
decimal CSP address, `<name>` for a parameter name, and `<path>` for an FTP
virtual path.

## Runtime and shell

| Command | Arguments | Description | Notes |
| --- | --- | --- | --- |
| `help` | `[command]` | Show Zephyr shell discovery and help | Built into the Zephyr shell |
| `status` | — | Show board and runtime status | Includes uptime and readiness state |
| `time` | — | Show monotonic milliseconds and microseconds | Not UTC, TAI, GNSS, or synchronized spacecraft time |
| `version` | — | Show the K-FSW and Zephyr versions and board | Suitable for operator identification |
| `log test` | — | Emit messages at compiled log levels | Diagnostic command |

```text
kfsw:~$ status
kfsw:~$ version
```

## CSP

| Command | Arguments | Description | Notes |
| --- | --- | --- | --- |
| `csp info` | — | Show local address, identity, router state, and free buffers | Read-only |
| `csp interfaces` | — | Show interface addresses and packet/error counters | Read-only snapshot |
| `csp routes` | — | Show configured static routes | Read-only snapshot |
| `csp ping` | `<node>` | Send a standard CRC32 CSP ping | Uses a 1 s timeout and bounded payload |

```text
kfsw:~$ csp ping 2
CSP ping 2: success, rtt_ms=...
```

## Dedicated UART / KISS

| Command | Arguments | Description | Notes |
| --- | --- | --- | --- |
| `uart info` | — | Show UART configuration, KISS status, peer, and counters | Present when UART/KISS is enabled |
| `uart test` | — | Verify the peer route and ping it over the UART interface | Requires a connected peer |

## Parameters

| Command | Arguments | Description | Notes |
| --- | --- | --- | --- |
| `param list` | `[node]` | List local or remote parameter descriptions | Remote descriptions are downloaded and cached |
| `param get` | `[node] <name>` | Read one local or remote scalar | Reports node prefix for remote values |
| `param set` | `[node] <name> <value>` | Change one local or remote scalar in RAM | Exact type/range validation; read-only values reject writes |
| `param save` | — | Atomically save persistent local RAM values | Does not save read-only `node_id` |
| `param load` | — | Reload the saved local snapshot into RAM | Reports when no snapshot exists |
| `param defaults` | — | Restore compiled local defaults in RAM | Saved snapshot remains unchanged |
| `param clear` | — | Delete the local snapshot | Current RAM remains unchanged |

```text
kfsw:~$ param get test_u32
test_u32 = 42
kfsw:~$ param set test_u32 1234
test_u32 = 1234
kfsw:~$ param save
Parameter snapshot save: PASS
```

Remote examples keep the node argument explicit:

```text
kfsw:~$ param get 2 test_u32
2:test_u32 = 42
kfsw:~$ param set 2 test_u32 1234
2:test_u32 = 1234
```

## Storage

| Command | Arguments | Description | Notes |
| --- | --- | --- | --- |
| `storage info` | — | Show filesystem, backend, mount point, readiness, and capacity | Current mount point is `/kfsw` |
| `storage test` | — | Run create/write/read/overwrite/delete checks | Uses bounded test data |
| `storage test write` | `<value>` | Write the persistence-test value | Integration helper |
| `storage test read` | `<value>` | Verify the persistence-test value | Integration helper |

```text
kfsw:~$ storage info
kfsw:~$ storage test
Storage test: PASS
```

## FTP

The preferred operator form places the node before the operation. The Zephyr
static-subcommand form is also accepted for tab completion.

| Command | Arguments | Description | Notes |
| --- | --- | --- | --- |
| `ftp <node> ls` | `[remote-directory]` | List one remote directory | `list` aliases `ls` |
| `ftp <node> stat` | `<remote-path>` | Show type, size, and CRC32 | File CRC is reported by the server |
| `ftp <node> mkdir` | `<remote-directory>` | Create one remote directory | Parent must exist |
| `ftp <node> put` | `<local-path> <remote-path>` | Upload a file | Paths are within each node's FTP sandbox |
| `ftp <node> get` | `<remote-path> <local-path>` | Download a file | Commits only after validation |
| `ftp generate` | `<local-path> <bytes>` | Create deterministic local test data | Bounded to 32768 bytes; diagnostic helper |
| `ftp verify` | `<first> <second>` | Compare two local sandbox files | Diagnostic helper |

Equivalent verb-first forms include `ftp put <node> ...`, `ftp get <node> ...`,
`ftp ls <node> ...`, `ftp stat <node> ...`, and `ftp mkdir <node> ...`.

```text
kfsw:~$ ftp generate /build/sample.bin 1024
kfsw:~$ ftp 2 mkdir /exchange
kfsw:~$ ftp put 2 /build/sample.bin /exchange/sample.bin
kfsw:~$ ftp 2 ls /exchange
kfsw:~$ ftp get 2 /exchange/sample.bin /build/returned.bin
kfsw:~$ ftp verify /build/sample.bin /build/returned.bin
```
