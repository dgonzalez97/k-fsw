*** Settings ***
Documentation    What survives an unplanned reset: the note saying why, and the
...              log ring with its numbering continued. native_sim re-executes
...              the process, so .noinit never carries over and neither can be
...              checked in simulation. Needs hardware.
Resource         resources/common.resource

*** Test Cases ***
A Reset Leaves Its Reason And The Log Behind
    [Documentation]    After a commanded reset the note must say it was
    ...    commanded and carry the previous run's uptime, the restart must be
    ...    counted, and the log ring must continue its sequence numbering
    ...    rather than restart it. The records from before the reset are gone
    ...    by then: the ring holds fewer than one boot writes.
    [Tags]    physical    nucleo    lastwords    logs    persistence    needs-nucleo
    ${result}=    Run Diagnostics Reset Smoke
    HIL Command Should Pass    ${result}    DIAGNOSTICS RESET SMOKE RESULT: PASS
    Should Contain    ${result.stdout}    the note says the reset was commanded
    Should Contain    ${result.stdout}    the restart was counted
    Should Contain    ${result.stdout}    the ring continued its numbering across the reset
    Should Contain    ${result.stdout}    the window moved with the ring
