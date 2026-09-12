*** Settings ***
Documentation    The board's clock across a reset.
...
...              Everything gated on a valid clock stays quiet without one, so
...              a node that reboots between passes would sit silent until a
...              ground station came into view to tell it the time. This is the
...              case that says it does not have to.
Resource         resources/common.resource

*** Test Cases ***
The Clock Outlives A Commanded Reset
    [Documentation]    Not merely restored: still counting. A value copied
    ...    before the reboot would read back exactly as it was set, so the test
    ...    requires a later time, and requires housekeeping to resume on its
    ...    own rather than waiting to be told.
    [Tags]    physical    nucleo    clock    rtc    hk
    ${result}=    Run Clock Smoke
    HIL Command Should Pass    ${result}    CLOCK SMOKE RESULT: PASS
    Should Contain    ${result.stdout}    the clock kept running across the reset
    Should Contain    ${result.stdout}    housekeeping resumed on its own
