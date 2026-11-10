# Required evidence

These selections map a small set of requirements to existing tests. A test name
is its exact suite path plus its case name; a bare case name is insufficient.
The manifest's `id` names the requirement and `source` points to its Robot file.
Both requirement IDs and selected test identities must be unique.
Selections may also declare `evidence_kind` as `hosted` or `physical`.

The optional [image binding check](image-binding.md) compares actual report,
image and resolved configuration hashes with an independently recorded expected
source/profile context. It checks consistency; the controlled runner must retain
the build and execution record that establishes the pair.

From the workspace root, check an executed software report:

```bash
python3 k-fsw/tools/ci/robot-counts.py build/robot/software/output.xml \
  --required k-fsw/tests/requirements/software.json
```

For the board stage's L496 Robot report:

```bash
python3 k-fsw/tools/ci/robot-counts.py build/robot/board/board/output.xml \
  --board --required k-fsw/tests/requirements/l496.json
```

An explicit `tests/hil/run.sh board` invocation defaults to `build/robot/output.xml`;
use its actual output path instead. The counter does not execute tests or flash.
Each selected test must appear exactly once with PASS. Missing, skipped, failed,
duplicate or malformed evidence fails. Unselected skips retain the normal
developer behavior; `KFSW_ROBOT_ACCEPTANCE=1` still requires zero software skips.
The Robot CI job checks the software selection after running its scenarios.
The fixtures run through `tests/ci-suite-test.py`, already part of quality checks.

| Requirement | Code or check | Software evidence | Physical evidence |
| --- | --- | --- | --- |
| Startup readiness | [Application startup](../../app/src/main.c) | `startup.hosted`: `Hil / Boot / Hosted Image Reports Boot And Ready` | `startup.l496`: `Hil / Boot / NUCLEO Reports Boot And Ready` |
| Saved parameters survive restart | [PARAM shell](../../app/src/shell/shell_param.c) | `parameters.persistence.hosted`: `Hil / Terminal / KFSW Linux Persists Parameters Across Restart` | Silicon reset and flash endurance are outside this selection. |
| `health.supervision`: an overdue component stops watchdog feeds | [Health registration](../../app/src/main.c), [unit cases](../unit/services_health/src/main.c) | `kfsw.services.health`: `services_health.test_a_component_is_late_before_it_is_overdue` checks deadlines and overdue reporting only. This unit suite has no watchdog and does not verify feed withdrawal. | [health-reset.sh](../hil/health/health-reset.sh) checks the watchdog reset and table 31 slots; standalone, outside the Robot gate. |
| Reset reason and log numbering survive reset | [Reset case](../hil/diagnostics-reset.robot) | native_sim re-executes; it cannot prove retained RAM. | `reset.retention.l496`: `Hil / Diagnostics-Reset / A Reset Leaves Its Reason And The Log Behind` |

The map records obligations, not current PASS claims. Physical evidence remains
NOT RUN until an observed bench run records its image, device identities and raw
results. A passing software selection does not cover health HIL or reset retention.
The L496 selection covers only its two named Robot cases; health's standalone
report must be checked separately. Do not use a dry-run report as execution
evidence. This counter checks report structure and identities; it does not attest
to report provenance, hardware identity, freshness or source revision.
