*** Settings ***
Documentation    What the board can tell you about a reset after it happened.
...
...              A node that restarts on its own is the case an operator cannot
...              watch. If nothing survives the reset, the next pass finds a
...              healthy node and no account of what went wrong. Two things are
...              meant to survive it: the note saying why, and the log messages
...              from the moments before.
...
...              Neither can be checked in simulation. native_sim re-executes the
...              process on a reset, so its address space is new and nothing in
...              .noinit ever carries over. This is the fixture that establishes
...              it on hardware.
Resource         resources/common.resource

*** Test Cases ***
A Reset Leaves Its Reason And The Log Behind
    [Documentation]    After a commanded reset the note must say it was
    ...    commanded and carry the previous run's uptime, the restart must be
    ...    counted, and the log ring must still hold records from before the
    ...    reset with its sequence numbering continued rather than restarted.
    [Tags]    physical    nucleo    lastwords    logs    persistence
    ${result}=    Run Diagnostics Reset Smoke
    HIL Command Should Pass    ${result}    DIAGNOSTICS RESET SMOKE RESULT: PASS
    Should Contain    ${result.stdout}    the note says the reset was commanded
    Should Contain    ${result.stdout}    the restart was counted
    Should Contain    ${result.stdout}    the ring continued its numbering across the reset
    Should Contain    ${result.stdout}    records from before the reset are still readable
