*** Settings ***
Documentation     CSP over CAN between a NUCLEO-L496ZG and a host CAN adapter.
...               Physical only: it needs the board, a transceiver and an
...               adapter on the same bus, so there is no software-tagged half.
Library           Process
Library           OperatingSystem

*** Variables ***
${CAN SMOKE}      ${CURDIR}/stm32/nucleo-l496zg/can-smoke.sh

*** Test Cases ***
CSP Reaches A Node Over CAN
    [Documentation]    Ping, identity and remote parameters over CAN, and the
    ...                adapter's frame counters.
    [Tags]    physical    can    nucleo    csp
    ${result}=    Run Process    ${CAN SMOKE}    --no-build
    ...           stdout=${TEMPDIR}/can-smoke.out    stderr=STDOUT    timeout=300s
    Log    ${result.stdout}
    Should Contain    ${result.stdout}    CAN SMOKE RESULT: PASS
    Should Contain    ${result.stdout}    ident=yes
    Should Contain    ${result.stdout}    params=yes
    Should Contain    ${result.stdout}    berr=tx0/rx0
