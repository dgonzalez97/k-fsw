*** Settings ***
Documentation     The image scrub against real flash on a NUCLEO-L496ZG.
...               Twister covers the state machine. Only hardware shows that the
...               reference outlives a reset and that a slot written behind the
...               service's back is caught when it is next re-read.
Resource          resources/common.resource
Library           Process

*** Variables ***
${SCRUB REFERENCE}    ${CURDIR}/scrub/learned-reference.sh

*** Test Cases ***
A Learned Reference Survives A Reset And Catches A Changed Slot
    [Documentation]    Learn slot0, reset, sweep, then write another image into
    ...                slot0 and sweep again. Needs the project signing key and
    ...                openocd, and reflashes the board.
    [Tags]    physical    nucleo    scrub    needs-nucleo
    ${result}=    Run Process    ${SCRUB REFERENCE}
    ...           stdout=${OUTPUT DIR}/scrub-reference.out    stderr=STDOUT    timeout=2400s
    Log    ${result.stdout}
    Skip If    "SCRUB HIL RESULT: NOT RUN" in $result.stdout    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    SCRUB HIL RESULT: PASS
    Should Contain    ${result.stdout}    slot0 reports unknown, not ok
    Should Contain    ${result.stdout}    the reference outlived the reset
    Should Contain    ${result.stdout}    a changed slot reports mismatch
