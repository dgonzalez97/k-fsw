# Interface control document {#icd}

Every shell command, remote command and parameter the sources define. Read from the sources and not from a running image, so an interface a particular configuration switches off is still described. The `Built when` column names the condition that has to hold for it to be in an image.

Mode letters: `r` read only, `rw` writable, `b` kept across a reset, `c` operator configuration, `i` system identity, `d` diagnostic, `o` owner persists it, `l` applied live, `n` local writes only.

## Shell commands

Read from the `SHELL_*` macros in the sources, so a command a particular image switches off is still listed.

| Command | Args | What it does | Built when | Defined in |
| --- | --- | --- | --- | --- |
| `button_test` | - | Button example diagnostics. | always | `kfsw-modules/button-test/src/button_test_shell.c` |
| `button_test status` | 0 | Show debounced button status. | always | `kfsw-modules/button-test/src/button_test_shell.c` |
| `cmd` | - | Read the command registry; each command runs from its own group. | always | `k-fsw/app/src/shell/shell_registry.c` |
| `cmd list` | 0 | Every command this node carries | always | `k-fsw/app/src/shell/shell_registry.c` |
| `cmd describe` | 0 | Every command with its arguments and its help | always | `k-fsw/app/src/shell/shell_registry.c` |
| `cmd show` | 1 | One command with its arguments: show <name> | always | `k-fsw/app/src/shell/shell_registry.c` |
| `comms` | - | Links: UART, CAN and radio. | always | `k-fsw/app/src/shell/shell_comms.c` |
| `comms can` | 0 | CSP over CAN. | always | `k-fsw/app/src/shell/shell_can.c` |
| `comms can info` | 0 | Show the CSP CAN interface | always | `k-fsw/app/src/shell/shell_can.c` |
| `comms can test` | 1 | Ping a node through CAN: test <node> | always | `k-fsw/app/src/shell/shell_can.c` |
| `comms uart` | 0 | CSP over UART, KISS framed. | always | `k-fsw/app/src/shell/shell_uart.c` |
| `comms uart info` | 0 | Show each dedicated CSP UART status | always | `k-fsw/app/src/shell/shell_uart.c` |
| `comms uart test` | 0-1 | Ping configured or explicit peer: test [node] | always | `k-fsw/app/src/shell/shell_uart.c` |
| `comms uhf` | 0 | UHF radio diagnostics. | `CONFIG_KFSW_RADIO_UHF_SHELL` | `kfsw-modules/radio-uhf/src/radio_uhf.c` |
| `comms uhf connect` | 0 | Establish radio sessions with the configured peer. | `CONFIG_KFSW_RADIO_UHF_CRYPTO` | `kfsw-modules/radio-uhf/src/radio_uhf.c` |
| `comms uhf status` | 0 | Show configured UHF radio identity and status. | `CONFIG_KFSW_RADIO_UHF_SHELL` | `kfsw-modules/radio-uhf/src/radio_uhf.c` |
| `csp` | - | CSP commands. | always | `k-fsw/app/src/shell/shell_csp.c` |
| `csp ifstat` | 2 | Read remote counters: ifstat <node> <interface> | always | `k-fsw/app/src/shell/shell_csp.c` |
| `csp clock` | 0-2 | Time: clock [set <utc>|<node> [sync]] | always | `k-fsw/app/src/shell/shell_csp.c` |
| `csp counters` | 0-1 | Show libcsp error counters: counters [clear] | always | `k-fsw/app/src/shell/shell_csp.c` |
| `csp debug` | 0-1 | Trace packets in and out: debug [on|off] | always | `k-fsw/app/src/shell/shell_csp.c` |
| `csp ident` | 0-1 | Identify a node, or this one when no node is given: ident [node] | always | `k-fsw/app/src/shell/shell_csp.c` |
| `csp info` | 0 | Show local CSP identity and router state | always | `k-fsw/app/src/shell/shell_csp.c` |
| `csp interfaces` | 0 | Show registered CSP interfaces | always | `k-fsw/app/src/shell/shell_csp.c` |
| `csp ping` | 0-1 | Ping a node, or this one when no node is given: ping [node] | always | `k-fsw/app/src/shell/shell_csp.c` |
| `csp reboot` | 2-3 | Restart a node: reboot <node> <pin> [--retry] | `CONFIG_KFSW_COMMAND && CONFIG_REBOOT` | `k-fsw/app/src/shell/shell_csp.c` |
| `csp rdp` | 0-1 | Reliable transport detail: rdp [0..2] | always | `k-fsw/app/src/shell/shell_csp.c` |
| `csp routes` | 0 | Show the CSP static routing table | always | `k-fsw/app/src/shell/shell_csp.c` |
| `event` | - | Event record. | always | `k-fsw/app/src/shell/shell_event.c` |
| `event clear` | 0 | Discard held records; counters are kept | always | `k-fsw/app/src/shell/shell_event.c` |
| `event list` | 0 | Show held records, oldest first | always | `k-fsw/app/src/shell/shell_event.c` |
| `event stats` | 0-1 | Show record counters, here or on a node: stats [node] | `CONFIG_KFSW_COMMAND_CSP` | `k-fsw/app/src/shell/shell_event.c` |
| `event tail` | 1-2 | One record by age, newest 0: tail [node] <age> | `CONFIG_KFSW_COMMAND_CSP` | `k-fsw/app/src/shell/shell_event.c` |
| `fbo` | - | File based operations: run a sequence from a file. | always | `k-fsw/app/src/shell/shell_fbo.c` |
| `fbo sched` | - | Time-tagged queue: release a command later. | always | `k-fsw/app/src/shell/shell_fbo.c` |
| `fbo sched add` | 3 | Release a command later: add <+s|@utc> <node> "<command>" | always | `k-fsw/app/src/shell/shell_fbo.c` |
| `fbo sched cancel` | 1 | Cancel a scheduled entry: cancel <index> | always | `k-fsw/app/src/shell/shell_fbo.c` |
| `fbo sched clear` | 0 | Empty the queue; a release in flight finishes | always | `k-fsw/app/src/shell/shell_fbo.c` |
| `fbo sched list` | 0 | Show every queued entry | always | `k-fsw/app/src/shell/shell_fbo.c` |
| `fbo sched status` | 0 | Show the queue counters | always | `k-fsw/app/src/shell/shell_fbo.c` |
| `fbo run` | 1 | Carry out a procedure: run <name> | always | `k-fsw/app/src/shell/shell_fbo.c` |
| `fbo stop` | 0 | Ask the running procedure to stop | always | `k-fsw/app/src/shell/shell_fbo.c` |
| `fbo status` | 0 | Show what a procedure has done | always | `k-fsw/app/src/shell/shell_fbo.c` |
| `ftp` | - | File transfer. Without a node, this node. | always | `k-fsw/app/src/shell/shell_ftp.c` |
| `ftp generate` | 2 | Create deterministic local data: generate <path> <bytes 0..32768> | always | `k-fsw/app/src/shell/shell_ftp.c` |
| `ftp get` | 3 | Download from a remote node: get <node> <remote> <local> | always | `k-fsw/app/src/shell/shell_ftp.c` |
| `ftp ls` | 0-2 | List a directory: ls [node] [path] | always | `k-fsw/app/src/shell/shell_ftp.c` |
| `ftp mkdir` | 1-2 | Create a directory: mkdir [node] <path> | always | `k-fsw/app/src/shell/shell_ftp.c` |
| `ftp put` | 3 | Upload to a remote node: put <node> <local> <remote> | always | `k-fsw/app/src/shell/shell_ftp.c` |
| `ftp stat` | 1-2 | Show metadata: stat [node] <path> | always | `k-fsw/app/src/shell/shell_ftp.c` |
| `ftp verify` | 2 | Compare two local files: verify <first> <second> | always | `k-fsw/app/src/shell/shell_ftp.c` |
| `fwu` | - | Firmware update. | always | `k-fsw/app/src/shell/shell_fwu.c` |
| `fwu abort` | 0 | Abandon a transfer and erase the slot | always | `k-fsw/app/src/shell/shell_fwu.c` |
| `fwu begin` | 2 | Start a transfer: begin <size> <crc32> | always | `k-fsw/app/src/shell/shell_fwu.c` |
| `fwu finish` | 0 | Verify the image and offer it to the bootloader | always | `k-fsw/app/src/shell/shell_fwu.c` |
| `fwu status` | 0 | Show update state and slot geometry | always | `k-fsw/app/src/shell/shell_fwu.c` |
| `fwu_lite` | - | Resumable firmware upload. | always | `k-fsw/app/src/shell/shell_fwu_lite.c` |
| `fwu_lite abort` | 1 | Erase this sender's transfer: abort <node> | always | `k-fsw/app/src/shell/shell_fwu_lite.c` |
| `fwu_lite flash` | 1 | Activate a validated image: flash <node> | always | `k-fsw/app/src/shell/shell_fwu_lite.c` |
| `fwu_lite send` | 2 | Upload or resume: send <node> <path> | always | `k-fsw/app/src/shell/shell_fwu_lite.c` |
| `fwu_lite status` | 0-1 | Show local or owned remote progress: status [node] | always | `k-fsw/app/src/shell/shell_fwu_lite.c` |
| `fwu_lite validate` | 1 | Validate stored bytes: validate <node> | always | `k-fsw/app/src/shell/shell_fwu_lite.c` |
| `gndwdt` | - | Ground watchdog. | always | `k-fsw/app/src/shell/shell_gndwdt.c` |
| `gndwdt feed` | 1 | Feed another node's ground watchdog: feed <node> | `CONFIG_KFSW_COMMAND_CSP` | `k-fsw/app/src/shell/shell_gndwdt.c` |
| `gndwdt off` | 0 | Disarm this node's countdown | always | `k-fsw/app/src/shell/shell_gndwdt.c` |
| `gndwdt on` | 0 | Arm this node's countdown | always | `k-fsw/app/src/shell/shell_gndwdt.c` |
| `gndwdt show` | 0-1 | Show the countdown, here or on another node: show [node] | `CONFIG_KFSW_COMMAND_CSP` | `k-fsw/app/src/shell/shell_gndwdt.c` |
| `gndwdt timeout` | 1 | Set the silence allowed: timeout <seconds> | always | `k-fsw/app/src/shell/shell_gndwdt.c` |
| `golden` | - | Recovery image and the startups left before it. | always | `k-fsw/app/src/shell/shell_golden.c` |
| `golden status` | 0 | Whether the golden partition holds an image | `CONFIG_KFSW_GOLDEN` | `k-fsw/app/src/shell/shell_golden.c` |
| `golden install` | 1-2 | Install it and restart: install [node] <pin> | `CONFIG_KFSW_GOLDEN && CONFIG_KFSW_COMMAND` | `k-fsw/app/src/shell/shell_golden.c` |
| `golden attempts` | 0-1 | Startups left before recovery: attempts [count] | `CONFIG_KFSW_BOOT_STORE && CONFIG_KFSW_PARAM` | `k-fsw/app/src/shell/shell_golden.c` |
| `health` | - | Health monitoring. | always | `k-fsw/app/src/shell/shell_health.c` |
| `health list` | 0 | Show each watched component and its deadline | always | `k-fsw/app/src/shell/shell_health.c` |
| `health report` | 1 | Report a component alive: report <handle> | always | `k-fsw/app/src/shell/shell_health.c` |
| `health status` | 0 | Show whether the watchdog is being fed, and why | always | `k-fsw/app/src/shell/shell_health.c` |
| `health watch` | 0-3 | Watch a component nothing reports: watch <name> <ms> confirm | always | `k-fsw/app/src/shell/shell_health.c` |
| `hk` | - | Housekeeping: collect a set of values in one pass. | always | `k-fsw/app/src/shell/shell_hk.c` |
| `hk save` | 0 | Save settings; replace a rejected snapshot | `CONFIG_KFSW_HK_PERSISTENCE` | `k-fsw/app/src/shell/shell_hk.c` |
| `hk define` | 2-5 | Name a report: define [node] <report> [class=N] <[node:]table:offset> ... [--retry] | `CONFIG_KFSW_COMMAND_CSP` | `k-fsw/app/src/shell/shell_hk.c` |
| `hk clear` | 1-3 | Forget a report: clear [node] <report> [--retry] | `CONFIG_KFSW_COMMAND_CSP` | `k-fsw/app/src/shell/shell_hk.c` |
| `hk show` | 0 | Show the reports and the counters | always | `k-fsw/app/src/shell/shell_hk.c` |
| `hk collect` | 1 | Collect now: collect <report> | always | `k-fsw/app/src/shell/shell_hk.c` |
| `hk get` | 1-2 | Read samples back: get <report> [count] | always | `k-fsw/app/src/shell/shell_hk.c` |
| `hk period` | 2-4 | Collect repeatedly; 0 to stop: period [node] <report> <ms> [--retry] | `CONFIG_KFSW_COMMAND_CSP` | `k-fsw/app/src/shell/shell_hk.c` |
| `hk store` | 2 | Keep samples in a file; 0 to stop: store <report> <ms> | `CONFIG_KFSW_HK_STORE` | `k-fsw/app/src/shell/shell_hk.c` |
| `hk store_clear` | 1 | Stop storing and delete the file: store_clear <report> | `CONFIG_KFSW_HK_STORE` | `k-fsw/app/src/shell/shell_hk.c` |
| `hk stored` | - | Read stored samples: stored <report> [from=<sequence>] [to=<sequence>] [since=<seconds>] [until=<seconds>] [skip=<flags>] | `CONFIG_KFSW_HK_STORE` | `k-fsw/app/src/shell/shell_hk.c` |
| `hk extract` | - | Stored samples to a file: extract <report> <path> [from=<sequence>] [to=<sequence>] [since=<seconds>] [until=<seconds>] [skip=<flags>] | `CONFIG_KFSW_HK_STORE` | `k-fsw/app/src/shell/shell_hk.c` |
| `hk beacon` | 3 | Send unprompted; 0 to stop,  at least: beacon <report> <node> <ms> | `CONFIG_KFSW_HK_BEACON` | `k-fsw/app/src/shell/shell_hk.c` |
| `journal` | - | Persistent event journal. | always | `k-fsw/app/src/shell/shell_journal.c` |
| `journal remote` | 1-2 | Download another node's newest records: remote <node> [count] | `CONFIG_KFSW_LOG_REMOTE` | `k-fsw/app/src/shell/shell_journal.c` |
| `journal stats` | - | Journal state, here or on a node: stats | always | `k-fsw/app/src/shell/shell_journal.c` |
| `journal tail` | - | One saved event by age, newest 0: tail <age> | always | `k-fsw/app/src/shell/shell_journal.c` |
| `journal time` | - | When a saved event happened: time <age> | always | `k-fsw/app/src/shell/shell_journal.c` |
| `limits` | - | Parameter limits. | always | `k-fsw/app/src/shell/shell_limits.c` |
| `limits list` | 0 | List the rules in force | always | `k-fsw/app/src/shell/shell_limits.c` |
| `limits load` | 0-1 | Load a rule file: load [file] | always | `k-fsw/app/src/shell/shell_limits.c` |
| `limits status` | 0 | Show counters and the last load result | always | `k-fsw/app/src/shell/shell_limits.c` |
| `limits sweep` | 0 | Evaluate the rules now | always | `k-fsw/app/src/shell/shell_limits.c` |
| `log` | - | Logging commands. | always | `k-fsw/app/src/shell/shell_system.c` |
| `log level` | 0-2 | Levels by service: level [<module> [0..4]] | always | `k-fsw/app/src/shell/shell_system.c` |
| `log history` | 0 | Read recent log messages | `CONFIG_KFSW_LOG_HISTORY` | `k-fsw/app/src/shell/shell_system.c` |
| `log remote` | 1-3 | Another node's recent messages: remote <node> [count] [level] | `CONFIG_KFSW_LOG_REMOTE` | `k-fsw/app/src/shell/shell_system.c` |
| `log test` | 0 | Exercise all log levels | always | `k-fsw/app/src/shell/shell_system.c` |
| `param` | - | Parameter commands. | always | `k-fsw/app/src/shell/shell_param.c` |
| `param clear` | 0 | Delete the saved snapshot; RAM is unchanged | `CONFIG_KFSW_PARAM_PERSISTENCE` | `k-fsw/app/src/shell/shell_param.c` |
| `param defaults` | 0 | Restore compiled defaults in RAM only | `CONFIG_KFSW_PARAM_PERSISTENCE` | `k-fsw/app/src/shell/shell_param.c` |
| `param get` | 1-2 | Get local or remote value: get [node] <name> | always | `k-fsw/app/src/shell/shell_param.c` |
| `reboot` | 1 | Restart this node; csp reboot for another: reboot <pin> | `CONFIG_KFSW_COMMAND && CONFIG_REBOOT` | `k-fsw/app/src/shell/shell_system.c` |
| `remexec` | - | What another node offers: remexec <node> get [command] | <node> run "<command>" | always | `k-fsw/app/src/shell/shell_remexec.c` |
| `resmon` | - | Resource monitor. | always | `k-fsw/app/src/shell/shell_resmon.c` |
| `resmon alert` | 1 | Set the stack use that raises an event: alert <percent> | always | `k-fsw/app/src/shell/shell_resmon.c` |
| `resmon sample` | 0 | Sweep the threads now | always | `k-fsw/app/src/shell/shell_resmon.c` |
| `resmon show` | 0 | Show what the last sweep found | always | `k-fsw/app/src/shell/shell_resmon.c` |
| `scrub` | - | Resident image checksums. | always | `k-fsw/app/src/shell/shell_scrub.c` |
| `scrub status` | 0 | Counters and what each region last reported | always | `k-fsw/app/src/shell/shell_scrub.c` |
| `scrub sweep` | 0-1 | Re-read every region, or one: sweep [slot0|slot1|golden] | always | `k-fsw/app/src/shell/shell_scrub.c` |
| `scrub learn` | 1 | Take what is on flash now as the reference: learn <region> | always | `k-fsw/app/src/shell/shell_scrub.c` |
| `status` | 0 | Show basic runtime status: status | always | `k-fsw/app/src/shell/shell_system.c` |
| `status` | 0-1 | Show status, here or on a node: status [node] | `CONFIG_KFSW_COMMAND_CSP` | `k-fsw/app/src/shell/shell_system.c` |
| `storage` | - | Filesystem storage commands. | always | `k-fsw/app/src/shell/shell_storage.c` |
| `storage info` | 0 | Show filesystem storage status | always | `k-fsw/app/src/shell/shell_storage.c` |
| `storage test` | 0-2 | Run storage test: test [write|read <value>] | always | `k-fsw/app/src/shell/shell_storage.c` |
| `table` | - | Parameter tables uploaded as files. | always | `k-fsw/app/src/shell/shell_table.c` |
| `table check` | 1 | Check a table file without applying it: check <path> | always | `k-fsw/app/src/shell/shell_table.c` |
| `table dump` | 2 | Write a table out as a file: dump <table> <path> | always | `k-fsw/app/src/shell/shell_table.c` |
| `table load` | 1 | Adopt a table file, all of it or none: load <path> | always | `k-fsw/app/src/shell/shell_table.c` |
| `table revert` | 0 | Put back the values the last load replaced | always | `k-fsw/app/src/shell/shell_table.c` |
| `table show` | 0 | Show what is loaded and the counters | always | `k-fsw/app/src/shell/shell_table.c` |
| `temp` | - | Temperature example: the MCU die reading. | always | `kfsw-modules/temperature-sensor-example/src/temperature_sensor_example_shell.c` |
| `temp status` | 0 | Show the cached die temperature. | always | `kfsw-modules/temperature-sensor-example/src/temperature_sensor_example_shell.c` |
| `test` | - | Developer hardware-test commands. | always | `kfsw-modules/button-test/src/button_test_shell.c` |
| `test led` | 2 | Control LED: led <green|blue|red> <on|off>. | always | `kfsw-modules/button-test/src/button_test_shell.c` |
| `time` | 0 | Show monotonic time: time | always | `k-fsw/app/src/shell/shell_system.c` |
| `version` | 0 | Show build information: version | always | `k-fsw/app/src/shell/shell_system.c` |
| `watchdog` | - | Hardware watchdog. | always | `k-fsw/app/src/shell/shell_watchdog.c` |
| `watchdog feed` | 0 | Feed the watchdog once | always | `k-fsw/app/src/shell/shell_watchdog.c` |
| `watchdog starve` | 0-1 | Stop feeding so the board resets: starve confirm | always | `k-fsw/app/src/shell/shell_watchdog.c` |
| `watchdog status` | 0 | Show watchdog configuration and activity | always | `k-fsw/app/src/shell/shell_watchdog.c` |

## Command registry

What a ground station or another node can ask this one to run over CSP. `Claim` marks a command that changes something and may only be sent by a caller that already holds the right to.

| ID | Command | Args | Claim | What it does | Built when | Defined in |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `noop` | 0 | - | Round trip with no effect. | always | `k-fsw/app/src/commands/command_definitions.c` |
| 2 | `info` | 0 | - | Report uptime and storage state. | always | `k-fsw/app/src/commands/command_definitions.c` |
| 3 | `reboot` | 1 | yes | Reset this node after a short delay: reboot <pin>. | `CONFIG_REBOOT` | `k-fsw/app/src/commands/command_definitions.c` |
| 4 | `event_stats` | 0 | - | Report event record counters. | `CONFIG_KFSW_EVENT` | `k-fsw/app/src/commands/command_definitions.c` |
| 5 | `event_tail` | 1 | - | Read one recorded event by age, newest is 0. | `CONFIG_KFSW_EVENT` | `k-fsw/app/src/commands/command_definitions.c` |
| 6 | `hk_define` | 2 | - | Name what a report collects: <report> "[class=N] [node:]table:offset ...". | `CONFIG_KFSW_HK` | `k-fsw/app/src/commands/command_definitions.c` |
| 7 | `hk_period` | 2 | - | Collect repeatedly: hk_period <report> <ms>, 0 to stop. | `CONFIG_KFSW_HK` | `k-fsw/app/src/commands/command_definitions.c` |
| 8 | `hk_clear` | 1 | - | Forget a report: hk_clear <report>. | `CONFIG_KFSW_HK` | `k-fsw/app/src/commands/command_definitions.c` |
| 9 | `journal_stats` | 0 | - | Read persistent journal status. | `CONFIG_KFSW_JOURNAL` | `k-fsw/app/src/commands/command_definitions.c` |
| 10 | `journal_tail` | 1 | - | Read a journal event by age, newest is 0. | `CONFIG_KFSW_JOURNAL` | `k-fsw/app/src/commands/command_definitions.c` |
| 11 | `journal_time` | 1 | - | Read a journal event's sequence and time by age. | `CONFIG_KFSW_JOURNAL` | `k-fsw/app/src/commands/command_definitions.c` |
| 16 | `ground_wtd` | 1 | yes | Ground watchdog: get, or KFSWWSFK to feed over CSP. | always | `kfsw-services/src/gndwdt/gndwdt.c` |
| 20 | `fbo_run` | 1 | yes | Carry out a procedure: fbo_run <name>. | always | `kfsw-services/src/fbo/fbo_commands.c` |
| 21 | `fbo_sched_add` | 3 | yes | Release a command later: fbo_sched_add <+s|@utc> <node> "<command>". | always | `kfsw-services/src/fbo/fbo_commands.c` |
| 22 | `fbo_sched_cancel` | 1 | yes | Cancel a scheduled entry: fbo_sched_cancel <index>. | always | `kfsw-services/src/fbo/fbo_commands.c` |
| 23 | `fbo_sched_clear` | 0 | yes | Empty the queue; a release in flight finishes. | always | `kfsw-services/src/fbo/fbo_commands.c` |
| 24 | `golden_install` | 1 | yes | Install the recovery image and restart: golden_install <pin>. | `CONFIG_KFSW_GOLDEN` | `k-fsw/app/src/commands/command_definitions.c` |
| 40 | `gpredict_select` | 1 | yes | Judge the next bearing by a profile: gpredict_select <index>. | always | `kfsw-modules/gpredict/src/gpredict_commands.c` |
| 41 | `gpredict_enable` | 2 | yes | Allow or forbid selecting a profile: gpredict_enable <index> <1|0>. | always | `kfsw-modules/gpredict/src/gpredict_commands.c` |
| 42 | `gpredict_park` | 0 | yes | Send the antenna to its park position now. | always | `kfsw-modules/gpredict/src/gpredict_commands.c` |
| 43 | `gpredict_clear` | 0 | yes | Clear a tracker fault and park. | always | `kfsw-modules/gpredict/src/gpredict_commands.c` |
| 64 | `temp_clear` | 0 | yes | Zero the temperature read, failure and alarm counters. | always | `kfsw-modules/temperature-sensor-example/src/temperature_sensor_example_commands.c` |

## Parameter tables

| Table | Name | Params | Holds | Built when | Defined in |
| --- | --- | --- | --- | --- | --- |
| 1 | `board` | 12 | Node identity and what the board carries | always | `k-fsw/app/src/parameters/board_table.c` |
| 2 | `system` | 3 | Boot delay, report period, reboot pin | always | `k-fsw/app/src/parameters/system_table.c` |
| 3 | `telemetry` | 5 | Uptime, storage and CSP buffers | always | `k-fsw/app/src/parameters/telemetry_table.c` |
| 4 | `csp` | 17 | CSP counters and the route table | always | `k-fsw/app/src/parameters/csp_table.c` |
| 5 | `storage` | 4 | Filesystem size, free space, mount | always | `k-fsw/app/src/parameters/storage_table.c` |
| 6 | `watchdog` | 7 | Hardware watchdog timing and feeds | always | `k-fsw/app/src/parameters/watchdog_table.c` |
| 25 | `log` | 7 | Log levels, colour and counters | `CONFIG_KFSW_PARAM` | `kfsw-services/src/log.c` |
| 26 | `param` | 10 | Parameter counts and snapshot saves | always | `kfsw-services/src/param-parameters/param_table.c` |
| 27 | `event` | 5 | Event record counters | always | `kfsw-services/src/event-parameters/event_table.c` |
| 28 | `command` | 7 | Command counters, timeout and echo | always | `kfsw-services/src/command-parameters/command_table.c` |
| 29 | `ftp` | 7 | File transfers and limits | always | `kfsw-services/src/ftp-parameters/ftp_table.c` |
| 30 | `fwu` | 13 | Firmware update state and checks | always | `kfsw-services/src/fwu-parameters/fwu_table.c` |
| 31 | `health` | 7 | Health monitor and watchdog feeds | always | `kfsw-services/src/health-parameters/health_table.c` |
| 32 | `boot` | 13 | Running image, revisions, restarts | always | `kfsw-services/src/boot-parameters/boot_table.c` |
| 33 | `hk` | 14 | Housekeeping reports and counters | always | `kfsw-services/src/hk-parameters/hk_table.c` |
| 34 | `fbo` | 19 | Procedure runs, progress and the time-tagged queue | always | `kfsw-services/src/fbo-parameters/fbo_table.c` |
| 35 | `gndwdt` | 8 | Ground watchdog countdown | always | `kfsw-services/src/gndwdt-parameters/gndwdt_table.c` |
| 36 | `resmon` | 10 | Thread stack use | always | `kfsw-services/src/resmon-parameters/resmon_table.c` |
| 37 | `table` | 7 | Uploaded table files, adoptions and reverts | always | `kfsw-services/src/table-parameters/table_parameters.c` |
| 38 | `remexec` | 9 | Remote shell execution and its refusals | always | `kfsw-services/src/remexec-parameters/remexec_table.c` |
| 39 | `scrub` | 17 | Resident image checksums and their references | always | `kfsw-services/src/scrub-parameters/scrub_table.c` |
| 40 | `limits` | 11 | Parameter limits | always | `kfsw-services/src/limits-parameters/limits_table.c` |
| 50 | `radio_uhf` | 17 | UHF radio identity, link and encryption | always | `kfsw-modules/radio-uhf/parameters/radio_uhf_table.c` |
| 51 | `temp_example` | 7 | Die temperature readings and alarm | always | `kfsw-modules/temperature-sensor-example/parameters/temperature_sensor_example_table.c` |
| 52 | `gpredict` | 19 | Pass tracking state, bearings and grace period | always | `kfsw-modules/gpredict/parameters/gpredict_table.c` |
| 67 | `hw_test` | 5 | Button presses and LED controls | always | `kfsw-modules/button-test/parameters/button_test_table.c` |

## Parameters

| Table | Offset | Name | Type | Mode | Unit | Holds |
| --- | --- | --- | --- | --- | --- | --- |
| `board` | 0x00 | `node_id` | u16 | ri | - | CSP address this node is running as |
| `board` | 0x10 | `uid` | str | ri | - | Unit identity, as reported by csp ident |
| `board` | 0x40 | `hw_id` | str | ri | - | Identifier this unit's silicon was manufactured with |
| `board` | 0x20 | `model` | str | ri | - | Composition this build identifies as |
| `board` | 0x30 | `revision` | str | ri | - | Build revision, as reported by csp ident |
| `board` | 0x02 | `csp_enabled` | u8 | ri | - | Whether CSP is composed into this build |
| `board` | 0x05 | `can_enabled` | u8 | ri | - | Whether the CAN interface is composed |
| `board` | 0x0c | `can_speed` | u32 | rw | - | CAN bitrate: 125000, 250000, 500000, 800000 or 1000000 |
| `board` | 0x03 | `kiss_enabled` | u8 | ri | - | Whether the KISS interface is composed |
| `board` | 0x04 | `reset_cause` | KFSW_PARAM_X32 | ri | - | Reset cause latched at boot |
| `board` | 0x08 | `shell_enabled` | u8 | ri | - | Whether the debug shell is composed |
| `board` | 0x09 | `storage_enabled` | u8 | ri | - | Whether persistent storage is composed |
| `system` | 0x10 | `reboot_pin` | str | rwbc | - | Digits a remote reboot must quote, 1 to 8 of them |
| `system` | 0x00 | `boot_delay_ms` | u16 | rwbc | ms | Delay before services start on the next boot |
| `system` | 0x02 | `app_report_ms` | u16 | rwbcl | ms | How often the application thread reports it is running |
| `telemetry` | 0x00 | `uptime_s` | u32 | r | s | Seconds since boot |
| `telemetry` | 0x04 | `fs_total_kb` | u32 | r | kB | Filesystem capacity, or zero while unmounted |
| `telemetry` | 0x08 | `fs_free_kb` | u32 | r | kB | Filesystem space available, or zero while unmounted |
| `telemetry` | 0x0c | `csp_buf_free` | u16 | r | - | CSP buffers available to the router |
| `telemetry` | 0x0e | `fs_mounted` | u8 | r | - | Whether the filesystem is mounted and usable |
| `csp` | 0x00 | `tx_packets` | u32 | r | - | Packets sent across every interface |
| `csp` | 0x04 | `rx_packets` | u32 | r | - | Packets received across every interface |
| `csp` | 0x08 | `tx_errors` | u32 | r | - | Transmit errors across every interface |
| `csp` | 0x0c | `rx_errors` | u32 | r | - | Receive errors across every interface |
| `csp` | 0x10 | `dropped` | u32 | r | - | Packets dropped across every interface |
| `csp` | 0x14 | `interfaces` | u8 | r | - | Interfaces registered with the router |
| `csp` | 0x16 | `buffer_out` | u8 | r | - | Times no packet buffer was free |
| `csp` | 0x17 | `conn_out` | u8 | r | - | Times no connection slot was free |
| `csp` | 0x18 | `conn_ovf` | u8 | r | - | Packets dropped by a full connection queue |
| `csp` | 0x19 | `conn_noroute` | u8 | r | - | Packets dropped with no route to the destination |
| `csp` | 0x1a | `invalid_reply` | u8 | r | - | Replies that matched no open connection |
| `csp` | 0x1b | `last_error` | u8 | r | - | Code of the last libcsp error |
| `csp` | 0x1c | `last_can_error` | u8 | r | - | Code of the last CAN framing error |
| `csp` | 0x1d | `rdp_trace` | u8 | rwcl | - | RDP detail: 0 off, 1 errors, 2 with the protocol |
| `csp` | 0x1e | `packet_trace` | u8 | rwcl | - | Packet dump: 0 off, 1 on |
| `csp` | 0x20 | `route_table` | str | r | - | Compiled routes in libcsp CIDR syntax |
| `csp` | 0x15 | `router_running` | u8 | r | - | Whether the CSP router thread is running |
| `storage` | 0x00 | `total_kb` | u32 | r | kB | Filesystem capacity, or zero while unmounted |
| `storage` | 0x04 | `free_kb` | u32 | r | kB | Filesystem space available, or zero while unmounted |
| `storage` | 0x08 | `used_pct` | u8 | r | % | Filesystem space in use, or zero while unmounted |
| `storage` | 0x09 | `mounted` | u8 | r | - | Whether the volume is mounted and usable |
| `watchdog` | 0x00 | `timeout_ms` | u32 | r | ms | Timeout installed in hardware at initialization |
| `watchdog` | 0x04 | `feed_interval_ms` | u32 | r | ms | Interval the keep-alive feeds at |
| `watchdog` | 0x08 | `feeds` | u32 | r | - | Feeds issued since boot; saturates |
| `watchdog` | 0x0c | `since_feed_ms` | u32 | r | ms | Milliseconds since the most recent feed |
| `watchdog` | 0x10 | `state` | u8 | r | - | Watchdog lifecycle state |
| `watchdog` | 0x11 | `device_bound` | u8 | r | - | Whether a watchdog device was bound at initialization |
| `watchdog` | 0x12 | `keepalive_owned` | u8 | r | - | Whether the platform keep-alive is the one feeding |
| `log` | 0x00 | `log_level` | u8 | rwbc | - | Runtime logging policy value |
| `log` | 0x10 | `log_levels` | data | rwbc | - | Minimum level per module, raising the global one for that module |
| `log` | 0x04 | `log_emitted` | u32 | r | - | Messages written since boot |
| `log` | 0x08 | `log_dropped` | u32 | r | - | Messages below the active level, so not written |
| `log` | 0x0c | `log_color` | u8 | rwcl | - | Colour log lines by severity; read every line |
| `log` | 0x0d | `log_shell` | u8 | rwcl | - | Mirror logs to active shells; 0 off, 1 on |
| `log` | 0x20 | `log_remote_format` | u8 | rwbcl | - | How log remote sends messages: 0 text, 1 dictionary |
| `param` | 0x00 | `param_count` | u16 | r | - | Parameters registered across every table |
| `param` | 0x02 | `param_tables` | u16 | r | - | Tables registered |
| `param` | 0x04 | `param_persistent` | u16 | r | - | Of those, the ones a snapshot carries |
| `param` | 0x08 | `param_saves` | u32 | r | - | Snapshots written since boot |
| `param` | 0x0c | `param_load_failures` | u32 | r | - | Snapshot loads that failed for a reason other than absence |
| `param` | 0x10 | `param_autosave` | u8 | rwbcl | - | Write a snapshot after every accepted change |
| `param` | 0x14 | `param_persist_bytes` | u32 | r | - | Space the last snapshot occupied |
| `param` | 0x18 | `param_persist_max_bytes` | u32 | r | - | Most space a snapshot is allowed |
| `param` | 0x1c | `param_requests_dropped` | u32 | r | - | Value requests dropped before worker admission |
| `param` | 0x20 | `param_read_resends` | u32 | r | - | Extra read request packets after timeouts since boot; saturates |
| `event` | 0x00 | `events_recorded` | u32 | r | - | Events accepted since start |
| `event` | 0x04 | `events_overwritten` | u32 | r | - | Events lost to the ring wrapping before they were read |
| `event` | 0x08 | `events_rejected` | u32 | r | - | Events refused for an invalid payload size or source |
| `event` | 0x0c | `events_held` | u16 | r | - | Records currently in the ring |
| `event` | 0x0e | `events_capacity` | u16 | r | - | Ring capacity |
| `command` | 0x00 | `cmd_invoked` | u32 | r | - | Invocations that reached a handler or were refused before one |
| `command` | 0x04 | `cmd_failed` | u32 | r | - | Invocations whose handler reported a failure |
| `command` | 0x08 | `cmd_unknown` | u32 | r | - | Invocations naming a command that is not registered |
| `command` | 0x0c | `cmd_rejected` | u32 | r | - | Invocations refused before the handler ran |
| `command` | 0x10 | `cmd_registered` | u16 | r | - | Commands in the frozen registry |
| `command` | 0x14 | `cmd_timeout_ms` | u32 | rwc | ms | Timeout for new remote invocations |
| `command` | 0x18 | `echo_enabled` | u8 | rwc | - | Console repeats what is typed at it |
| `ftp` | 0x00 | `ftp_transfers` | u32 | r | - | Transfers that committed, in either direction |
| `ftp` | 0x04 | `ftp_failures` | u32 | r | - | Transfers that failed or were abandoned |
| `ftp` | 0x08 | `ftp_bytes` | u32 | r | B | Bytes moved by transfers that committed |
| `ftp` | 0x0c | `ftp_timeout_ms` | u32 | rwc | ms | Reply timeout for new transfers; a running one keeps its value |
| `ftp` | 0x10 | `ftp_chunk_size` | u16 | rwc | B | File data per message; can only be shortened |
| `ftp` | 0x12 | `ftp_busy` | u8 | r | - | Whether the server is handling a transfer |
| `ftp` | 0x20 | `ftp_root` | str | r | - | Sandbox the service resolves every path against |
| `fwu` | 0x00 | `fwu_total_size` | u32 | r | B | Image size the sender declared |
| `fwu` | 0x04 | `fwu_received` | u32 | r | B | Bytes accepted so far |
| `fwu` | 0x08 | `fwu_expected_crc` | KFSW_PARAM_X32 | r | - | CRC32 the sender declared; IEEE, not the CSP variant |
| `fwu` | 0x0c | `fwu_actual_crc` | KFSW_PARAM_X32 | r | - | CRC32 accumulated over what was accepted |
| `fwu` | 0x10 | `fwu_started` | u32 | r | - | Transfers begun since boot |
| `fwu` | 0x14 | `fwu_completed` | u32 | r | - | Transfers that reached ready since boot |
| `fwu` | 0x18 | `fwu_failed` | u32 | r | - | Transfers that failed or were aborted since boot |
| `fwu` | 0x1c | `fwu_state` | u8 | r | - | idle, receiving, ready or failed |
| `fwu` | 0x1d | `fwu_swap_scheduled` | u8 | r | - | Whether the bootloader accepted the image |
| `fwu` | 0x1e | `fwu_target_bound` | u8 | r | - | Whether a target partition was bound at build time |
| `fwu` | 0x20 | `fwu_lite_timeout_ms` | u32 | rwc | ms | Reply timeout for the next block |
| `fwu` | 0x24 | `fwu_lite_retries` | u8 | rwc | - | Times a block is repeated before the transfer is abandoned |
| `fwu` | 0x26 | `fwu_lite_block_size` | u16 | r | B | Image bytes per block; fixed at build time |
| `health` | 0x00 | `health_faulted_by` | str | r | - | Component holding the system faulted, or empty |
| `health` | 0x10 | `health_faults` | u32 | r | - | Times the system was judged unwell since boot |
| `health` | 0x14 | `health_feeds` | u32 | r | - | Watchdog feeds this service has issued |
| `health` | 0x18 | `health_interval_ms` | u16 | rwc | ms | How often deadlines are checked; refused above the feed interval |
| `health` | 0x1a | `health_state` | u8 | r | - | ok, faulted or stopped |
| `health` | 0x1b | `health_feeding` | u8 | r | - | Whether health is the one feeding the watchdog |
| `health` | 0x1c | `health_components` | u8 | r | - | Components currently watched |
| `boot` | 0x2b | `boot_attempts_left` | u8 | rwbo | - | Attempts remaining; ground writes persist in early flash |
| `boot` | 0x3c | `boot_attempts_max` | u8 | rwbo | - | Boot attempt limit: 3, 5 or 10; raising never refills |
| `boot` | 0x2c | `boot_attempts` | u32 | r | - | Persisted unconfirmed boots of this image; UINT32_MAX invalid |
| `boot` | 0x29 | `boot_revert_reason` | u8 | r | - | 0 none, 1 missed confirmation, 2 replaced unknown, 255 invalid |
| `boot` | 0x2a | `boot_trial_valid` | u8 | r | - | Trial diagnostics were recovered and saved successfully |
| `boot` | 0x00 | `boot_image` | str | ri | - | Version of the running image, from the build |
| `boot` | 0x40 | `boot_revisions` | str | ri | - | Short revision of each repository compiled into this image |
| `boot` | 0x20 | `boot_count` | u32 | rb | - | Restarts recorded across the life of the node |
| `boot` | 0x24 | `boot_reset_cause` | KFSW_PARAM_X32 | r | - | Cause latched at boot, before the flags were cleared |
| `boot` | 0x30 | `last_reason` | u8 | r | - | 0 quiet, 1 commanded, 2 brownout, 3 fatal, 4 starved, 5 unknown |
| `boot` | 0x34 | `last_detail` | KFSW_PARAM_X32 | r | - | Meaningful with the reason: the faulting address for a fatal |
| `boot` | 0x38 | `last_uptime_ms` | u32 | r | ms | How long the previous run had been up when it went away |
| `boot` | 0x28 | `boot_confirmed` | u8 | rwcl | - | Mark the running image good; can only be set, never cleared |
| `hk` | 0x30 | `hk_classes` | data | r | - | Definition classes by report; 255 undefined; define to change |
| `hk` | 0x00 | `hk_reports` | u8 | r | - | Reports currently defined |
| `hk` | 0x01 | `hk_enabled` | u8 | rwc | - | Collect periodic reports |
| `hk` | 0x02 | `hk_collections` | u32 | r | - | Reports collected since boot |
| `hk` | 0x06 | `hk_failures` | u32 | r | - | Collections that produced nothing |
| `hk` | 0x0a | `hk_entries_failed` | u32 | r | - | Values that could not be read and were left absent |
| `hk` | 0x0e | `hk_overwritten` | u32 | r | - | Samples the ring dropped to make room |
| `hk` | 0x12 | `hk_last_seconds` | u32 | r | s | When the last collection started, UTC |
| `hk` | 0x16 | `hk_clock_valid` | u8 | rl | - | Whether the node has a clock, and so collects on its period |
| `hk` | 0x20 | `hk_beacons_sent` | u32 | r | - | Samples put on the link without being asked |
| `hk` | 0x24 | `hk_beacons_skipped` | u32 | r | - | Beacons not sent because CSP buffers were short |
| `hk` | 0x1a | `hk_period_floor_ms` | u32 | r | ms | Shortest period a report may be given |
| `hk` | 0x1e | `hk_history` | u8 | r | - | Samples each report keeps |
| `hk` | 0x1f | `hk_max_entries` | u8 | r | - | Values one report may name |
| `fbo` | 0x00 | `fbo_running` | u8 | rl | - | Whether a procedure is running now |
| `fbo` | 0x01 | `fbo_runs` | u32 | r | - | Procedures started since boot |
| `fbo` | 0x05 | `fbo_lines_run` | u32 | r | - | Lines carried out since boot |
| `fbo` | 0x09 | `fbo_lines_failed` | u32 | r | - | Lines that reported a failure |
| `fbo` | 0x0d | `fbo_lines_skipped` | u32 | r | - | Lines a guard decided were not for this run |
| `fbo` | 0x11 | `fbo_line` | u16 | rl | - | Line the current or last run reached, 1-based |
| `fbo` | 0x20 | `fbo_procedure` | str | rl | - | Procedure running now, or the last one that ran |
| `fbo` | 0x40 | `fbo_lines_max` | u16 | r | - | Lines one procedure may carry out |
| `fbo` | 0x50 | `fbo_sched_entries` | u16 | rl | - | Slots the queue is holding |
| `fbo` | 0x54 | `fbo_sched_scheduled` | u16 | rl | - | Entries still waiting for their time |
| `fbo` | 0x56 | `fbo_sched_overdue` | u16 | rl | - | Entries whose time passed beyond the buffer |
| `fbo` | 0x58 | `fbo_sched_clock_set` | u8 | rl | - | Whether an absolute entry can be accepted at all |
| `fbo` | 0x59 | `fbo_sched_next_due_s` | u32 | rl | s | Seconds to the earliest relative entry |
| `fbo` | 0x5d | `fbo_sched_releases` | u32 | r | - | Releases attempted since boot |
| `fbo` | 0x61 | `fbo_sched_refusals` | u32 | r | - | Entries refused when they were added |
| `fbo` | 0x65 | `fbo_sched_overdues` | u32 | r | - | Entries that went overdue since boot |
| `fbo` | 0x69 | `fbo_sched_hash` | u32 | rl | - | CRC32 over the queue, to compare against what was uploaded |
| `fbo` | 0x6d | `fbo_sched_latency_s` | u32 | rwc | s | An entry later than this reports overdue instead of running |
| `fbo` | 0x71 | `fbo_sched_next_due_utc` | i64 | rl | - | UTC second of the earliest absolute entry, 0 for none |
| `gndwdt` | 0x00 | `gndwdt_enabled` | u8 | rw | - | Whether the countdown is armed |
| `gndwdt` | 0x04 | `ground_wtd_timeout` | u32 | rw | s | Timeout in seconds, 7200 to 432000 |
| `gndwdt` | 0x08 | `gndwdt_since_s` | u32 | r | s | Seconds since the last contact |
| `gndwdt` | 0x0c | `gndwdt_contacts` | u32 | r | - | Valid ground_wtd feeds since boot |
| `gndwdt` | 0x10 | `gndwdt_expiries` | u32 | r | - | Times the timeout passed since start |
| `gndwdt` | 0x14 | `gndwdt_last_node` | u16 | r | - | Node of the most recent contact |
| `gndwdt` | 0x16 | `gndwdt_running` | u8 | r | - | Whether the service has been started |
| `gndwdt` | 0x18 | `ground_wtd_cnt` | u32 | r | s | Seconds remaining on the ground watchdog |
| `resmon` | 0x00 | `stack_worst_used` | u32 | r | % | Highest stack use seen on any thread since start |
| `resmon` | 0x04 | `stack_last_used` | u32 | r | % | Highest stack use in the most recent sweep |
| `resmon` | 0x08 | `stack_alert_used` | u32 | rw | % | A thread at or above this raises an event |
| `resmon` | 0x0c | `stack_worst_free` | u32 | r | B | Unused bytes on the busiest thread |
| `resmon` | 0x10 | `stack_worst_size` | u32 | r | B | Stack size of the busiest thread |
| `resmon` | 0x14 | `stack_sweeps` | u32 | r | - | Sweeps completed since start |
| `resmon` | 0x18 | `stack_alerts` | u32 | r | - | Times a sweep first found a thread at the alert level |
| `resmon` | 0x1c | `stack_threads` | u16 | r | - | Threads the last sweep could read |
| `resmon` | 0x1e | `stack_running` | u8 | r | - | Whether the periodic sweep is running |
| `resmon` | 0x20 | `stack_worst_thread` | str | r | - | Thread holding the highest stack use |
| `table` | 0x00 | `table_state` | u8 | r | - | Whether a file is loaded and can still be reverted |
| `table` | 0x01 | `table_last` | u8 | r | - | Parameter table the last loaded file addressed |
| `table` | 0x02 | `table_entries` | u16 | r | - | Entries the last loaded file carried |
| `table` | 0x04 | `table_loads` | u32 | r | - | Files adopted since boot |
| `table` | 0x08 | `table_rejections` | u32 | r | - | Files refused since boot |
| `table` | 0x0c | `table_reverts` | u32 | r | - | Loads undone since boot |
| `table` | 0x10 | `table_path` | str | r | - | File of the last adopted load |
| `remexec` | 0x00 | `remexec_accepted` | u32 | r | - | Requests whose command was allowed and run |
| `remexec` | 0x04 | `remexec_refused` | u32 | r | - | Requests refused before any command ran |
| `remexec` | 0x08 | `remexec_truncated` | u32 | r | - | Replies that could not carry all the output |
| `remexec` | 0x0c | `remexec_offered` | u16 | r | - | Commands marked for remote execution |
| `remexec` | 0x10 | `remexec_refusal` | str | r | - | Why the most recent request was refused |
| `remexec` | 0x14 | `remexec_running` | u8 | r | - | Whether a command is running on this node now |
| `remexec` | 0x18 | `remexec_last_result` | i32 | r | - | What the last command returned; zero before any ran |
| `remexec` | 0x1c | `remexec_last_ms` | u32 | r | ms | How long the last command took |
| `remexec` | 0x20 | `remexec_last_command` | str | r | - | The last command line asked for, with its arguments |
| `scrub` | 0x00 | `slot0_reference` | KFSW_PARAM_X32 | rwb | - | Learned CRC32 of the primary slot image; 0 unlearned |
| `scrub` | 0x04 | `slot0_measured` | KFSW_PARAM_X32 | r | - | CRC32 the last sweep read from the primary slot |
| `scrub` | 0x08 | `slot0_bytes` | u32 | r | - | Image bytes swept in the primary slot |
| `scrub` | 0x0c | `slot0_state` | u8 | r | - | 0 empty, 1 unknown, 2 ok, 3 mismatch, 4 unreadable |
| `scrub` | 0x10 | `slot1_reference` | KFSW_PARAM_X32 | rwb | - | Learned CRC32 of the secondary slot image; 0 unlearned |
| `scrub` | 0x14 | `slot1_measured` | KFSW_PARAM_X32 | r | - | CRC32 the last sweep read from the secondary slot |
| `scrub` | 0x18 | `slot1_bytes` | u32 | r | - | Image bytes swept in the secondary slot |
| `scrub` | 0x1c | `slot1_state` | u8 | r | - | 0 empty, 1 unknown, 2 ok, 3 mismatch, 4 unreadable |
| `scrub` | 0x20 | `golden_reference` | KFSW_PARAM_X32 | rwb | - | Learned CRC32 of the recovery image; 0 unlearned |
| `scrub` | 0x24 | `golden_measured` | KFSW_PARAM_X32 | r | - | CRC32 the last sweep read from the recovery image |
| `scrub` | 0x28 | `golden_bytes` | u32 | r | - | Image bytes swept in the recovery partition |
| `scrub` | 0x2c | `golden_state` | u8 | r | - | 0 empty, 1 unknown, 2 ok, 3 mismatch, 4 unreadable |
| `scrub` | 0x30 | `scrub_sweeps` | u32 | r | - | Sweeps completed since start; saturates |
| `scrub` | 0x34 | `scrub_mismatches` | u32 | r | - | Regions found changed, counted once per crossing |
| `scrub` | 0x38 | `scrub_last_ms` | u32 | r | - | Monotonic ms at the end of the last sweep |
| `scrub` | 0x3c | `scrub_regions` | u8 | r | - | Regions this build can sweep |
| `scrub` | 0x3d | `scrub_running` | u8 | r | - | Whether the periodic sweep is running |
| `limits` | 0x00 | `limits_rules` | u8 | r | - | Rules in force |
| `limits` | 0x01 | `limits_running` | u8 | r | - | Whether the periodic sweep is scheduled |
| `limits` | 0x02 | `limits_enabled` | u8 | rwbl | - | 0 stops the sweeps and keeps the loaded rules |
| `limits` | 0x04 | `limits_interval_ms` | u32 | rwbl | ms | Between periodic sweeps; 0 or 100 to 3600000 |
| `limits` | 0x08 | `limits_sweeps` | u32 | r | - | Sweeps completed while enabled |
| `limits` | 0x0c | `limits_fires` | u32 | r | - | Actions released |
| `limits` | 0x10 | `limits_failures` | u32 | r | - | Parameter reads and releases that failed |
| `limits` | 0x14 | `limits_parse_errors` | u32 | r | - | Rule files refused for a bad line |
| `limits` | 0x18 | `limits_last_fired_ms` | u32 | r | ms | Uptime of the last release; 0 before any |
| `limits` | 0x1c | `limits_last_fired` | u8 | r | - | Index of the rule that fired last; 255 before any |
| `limits` | 0x1e | `limits_error_line` | u16 | r | - | Line that got the last file refused; 0 for none |
| `radio_uhf` | 0x00 | `uhf_implementation` | str | ri | - | Radio implementation this build selected |
| `radio_uhf` | 0x20 | `uhf_expected_hardware` | str | ri | - | Hardware this composition expects; not a live readback |
| `radio_uhf` | 0x40 | `uhf_expected_baud` | u32 | r | baud | Serial rate the radio profile expects; not applied here |
| `radio_uhf` | 0x44 | `uhf_expected_flow` | u8 | r | - | Whether the profile expects hardware flow control |
| `radio_uhf` | 0x45 | `uhf_status_available` | u8 | r | - | Whether this implementation reads the modem back at all |
| `radio_uhf` | 0x46 | `uhf_link_state` | u8 | r | - | unknown, down or up; unknown unless the modem is read back |
| `radio_uhf` | 0x50 | `uhf_key_hex` | str | rwcn | - | Local 64-digit hex key; reads return empty |
| `radio_uhf` | 0x98 | `uhf_encrypt_enable` | u8 | rwcn | - | Enable radio packet protection |
| `radio_uhf` | 0x99 | `uhf_encrypt_tx` | u8 | rwcn | - | Encrypt transmitted radio packets |
| `radio_uhf` | 0x9a | `uhf_encrypt_rx` | u8 | rwcn | - | Require authenticated encrypted radio packets |
| `radio_uhf` | 0x9b | `uhf_key_set` | u8 | r | - | - |
| `radio_uhf` | 0x9c | `uhf_tx_ready` | u8 | r | - | - |
| `radio_uhf` | 0x9d | `uhf_rx_ready` | u8 | r | - | - |
| `radio_uhf` | 0xa0 | `uhf_authenticated` | u32 | r | - | - |
| `radio_uhf` | 0xa4 | `uhf_rejected` | u32 | r | - | - |
| `radio_uhf` | 0xa8 | `uhf_replays` | u32 | r | - | - |
| `radio_uhf` | 0xac | `uhf_crypto_error` | i32 | r | - | - |
| `temp_example` | 0x00 | `temp_mcu_mc` | i32 | rl | mC | Die temperature in thousandths of a degree Celsius |
| `temp_example` | 0x04 | `temp_samples` | u32 | r | - | Successful sensor reads since boot |
| `temp_example` | 0x08 | `temp_failures` | u32 | r | - | Sensor reads that returned an error |
| `temp_example` | 0x0c | `temp_last_ms` | u32 | r | ms | Uptime of the latest successful read |
| `temp_example` | 0x10 | `temp_valid` | u8 | r | - | Whether temp_mcu_mc is a reading or the reserved value |
| `temp_example` | 0x14 | `temp_alarms` | u32 | r | - | Readings that crossed the alarm threshold upwards |
| `temp_example` | 0x18 | `temp_alarm_mc` | i32 | rwbc | mC | Alarm threshold, -40000 to 125000 |
| `gpredict` | 0x00 | `gp_state` | u8 | rl | - | parked, tracking, holding, parking or fault |
| `gpredict` | 0x04 | `gp_az_mdeg` | i32 | rl | mdeg | Azimuth last accepted from the predictor |
| `gpredict` | 0x08 | `gp_el_mdeg` | i32 | rl | mdeg | Elevation last accepted from the predictor |
| `gpredict` | 0x0c | `gp_actual_az_mdeg` | i32 | rl | mdeg | Azimuth the rotator last reported, not the commanded one |
| `gpredict` | 0x10 | `gp_actual_el_mdeg` | i32 | rl | mdeg | Elevation the rotator last reported, not the commanded one |
| `gpredict` | 0x18 | `gp_frequency_hz` | u64 | rl | Hz | Doppler-corrected frequency last accepted; 0 if none |
| `gpredict` | 0x20 | `gp_silence_ms` | u32 | rl | ms | Since the last bearing; 0 while none has arrived |
| `gpredict` | 0x24 | `gp_grace_ms` | u32 | rwc | ms | Silence the antenna holds through before it parks |
| `gpredict` | 0x28 | `gp_bearings` | u32 | r | - | Bearings accepted since boot; saturates |
| `gpredict` | 0x2c | `gp_refusals` | u32 | r | - | Bearings refused outside the travel or in fault; saturates |
| `gpredict` | 0x30 | `gp_passes_ended` | u32 | r | - | Passes that ended because the predictor went quiet |
| `gpredict` | 0x34 | `gp_faults` | u32 | r | - | Entries into fault since boot; saturates |
| `gpredict` | 0x38 | `gp_last_error` | i32 | r | - | Why the last refusal or fault happened, or 0 |
| `gpredict` | 0x3c | `gp_profile` | u8 | rl | - | Selected profile slot; 255 when none is selected |
| `gpredict` | 0x3d | `gp_policy` | u8 | rl | - | Frequency policy: 0 follow, 1 hold, 2 ignore; 255 when none |
| `gpredict` | 0x3e | `gp_profiles` | u8 | r | - | Profile slots holding a profile |
| `gpredict` | 0x3f | `gp_profiles_on` | u8 | r | - | Defined profiles that may be selected |
| `gpredict` | 0x40 | `gp_catalogue` | u32 | rl | - | Catalogue number of the selected profile; 0 when none |
| `gpredict` | 0x44 | `gp_profile_name` | str | rl | - | Name of the selected profile; empty when none |
| `hw_test` | 0x00 | `press_count` | u32 | rd | count | Valid debounced button presses since boot |
| `hw_test` | 0x04 | `last_press_s` | u32 | rd | s | Monotonic whole seconds of the latest valid press |
| `hw_test` | 0x08 | `led_green` | u8 | rwd | - | Logical green developer LED state |
| `hw_test` | 0x09 | `led_blue` | u8 | rwd | - | Logical blue developer LED state |
| `hw_test` | 0x0a | `led_red` | u8 | rwd | - | Logical red developer LED state |
