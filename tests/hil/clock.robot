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
    [Documentation]    The clock must still be counting after the reboot, so
    ...    everything gated on a valid clock keeps working without a pass.
    [Tags]    physical    nucleo    clock    rtc    hk
    ${result}=    Run Clock Smoke
    HIL Command Should Pass    ${result}    CLOCK SMOKE RESULT: PASS
    Should Contain    ${result.stdout}    the clock kept running across the reset
