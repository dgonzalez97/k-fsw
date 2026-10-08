*** Settings ***
Documentation     CSP over CAN between a NUCLEO-L496ZG and a host CAN adapter.
...               The physical case needs a transceiver and adapter on the same bus.
...               The hosted case uses vcan0 and proves no physical wiring.
Library           Process
Library           OperatingSystem

*** Variables ***
${CAN SMOKE}      ${CURDIR}/stm32/nucleo-l496zg/can-smoke.sh

*** Test Cases ***
CSP Reaches A Node Over CAN
    [Documentation]    Ping, identity and remote parameters over CAN, and the
    ...                adapter's frame counters with no bus errors added during the run.
    [Tags]    physical    can    nucleo    csp
    ${result}=    Run Process    ${CAN SMOKE}    --no-build
    ...           stdout=${TEMPDIR}/can-smoke.out    stderr=STDOUT    timeout=300s
    Log    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    CAN SMOKE RESULT: PASS
    Should Contain    ${result.stdout}    ident=yes
    Should Contain    ${result.stdout}    params=yes
    Should Contain    ${result.stdout}    berr_unchanged=yes

CSP Reaches A Node Over A Virtual CAN Bus
    [Documentation]    Hosted SocketCAN framing on vcan0; no transceiver or physical bitrate evidence.
    [Tags]    software    csp    can-virtual
    ${ready}=    Run Process    sh    -c    command -v ip >/dev/null && ip link show vcan0    stderr=STDOUT
    Skip If    ${ready.rc} != 0    Install iproute2 and create vcan0 with sudo k-fsw/tests/vcan-up.sh.
    ${result}=    Run Process    ${CURDIR}/../can-smoke.sh
    ...    stdout=${OUTPUT DIR}/virtual-can.out    stderr=STDOUT
    ...    timeout=300    env:KFSW_CAN_INTERFACE=vcan0
    Log    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    CAN RESULT: PASS
    Should Contain    ${result.stdout}    CSP ping 2: success
    Should Contain    ${result.stdout}    CSP ping 16: success
    Should Contain    ${result.stdout}    2:node_id = 2
    Should Contain    ${result.stdout}    last_can_error=0 (none)
