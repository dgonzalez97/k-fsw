*** Settings ***
Documentation    What survives an unplanned reset: the note saying why, and the
...              log messages from just before. native_sim re-executes the
...              process, so .noinit never carries over and neither can be
...              checked in simulation. Needs hardware.
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
