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
    [Documentation]    Ping, identity and remote parameters across the bus, and
    ...                the adapter's own frame counters as evidence the link
    ...                carried them.
    [Tags]    physical    can    nucleo    csp
    ${result}=    Run Process    ${CAN SMOKE}    --no-build
    ...           stdout=${TEMPDIR}/can-smoke.out    stderr=STDOUT    timeout=300s
    Log    ${result.stdout}
    Should Contain    ${result.stdout}    CAN SMOKE RESULT: PASS
    Should Contain    ${result.stdout}    ident=yes
    Should Contain    ${result.stdout}    params=yes
    Should Contain    ${result.stdout}    berr=tx0/rx0
