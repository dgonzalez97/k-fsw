*** Settings ***
Documentation     CSP over CAN between a NUCLEO-L496ZG and a host CAN adapter.
...               The physical case needs a transceiver and adapter on the same bus.
...               The hosted case uses vcan0 and proves no physical wiring.
Resource          resources/common.resource
Library           Process
Library           OperatingSystem

*** Variables ***
${CAN SMOKE}      ${CURDIR}/stm32/nucleo-l496zg/can-smoke.sh
${CAN ACCOUNTING}    ${CURDIR}/stm32/nucleo-l496zg/can-tx-accounting.sh
${CAN MESH}          ${CURDIR}/can-mesh-smoke.sh

*** Test Cases ***
CSP Reaches A Node Over CAN
    [Documentation]    Ping, identity and remote parameters over CAN, and the
    ...                adapter's frame counters with no bus errors added during the run.
    [Tags]    physical    can    nucleo    csp    needs-can
    ${result}=    Run Process    ${CAN SMOKE}
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
    Response Should Contain    ${result.stdout}    CSP ping 2: success
    Response Should Contain    ${result.stdout}    CSP ping 16: success
    Response Should Contain    ${result.stdout}    2:node_id = 2
    Response Should Contain    ${result.stdout}    last_can_error=0 (none)

A Frame Nobody Acknowledges Is Not Counted As Sent
    [Documentation]    With no node able to acknowledge, the board must count a
    ...                transmission as failed. A driver that reports an
    ...                unacknowledged frame as sent tells an operator the link
    ...                works when nothing left the node. Needs the bus down.
    [Tags]    physical    can    nucleo    needs-can
    ${result}=    Run Process    ${CAN ACCOUNTING}
    ...           stdout=${TEMPDIR}/can-tx-accounting.out    stderr=STDOUT    timeout=120s
    Log    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    # A case that did not run is not a case that passed.
    Skip If    "CAN TX ACCOUNTING RESULT: NOT RUN" in $result.stdout    ${result.stdout}
    Should Contain    ${result.stdout}    CAN TX ACCOUNTING RESULT: PASS

CSP Reaches Every Node Over CAN On Version 2
    [Documentation]    Both boards and the ground node on one bus: board to
    ...                board with no host in the path, and ground to each.
    [Tags]    physical    can    nucleo    csp    needs-can
    ${result}=    Run Process    ${CAN MESH}    --csp-version    2
    ...           stdout=${TEMPDIR}/can-mesh-2.out    stderr=STDOUT    timeout=1800s
    Log    ${result.stdout}
    Skip If    "CAN MESH RESULT: NOT RUN" in $result.stdout    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    CAN MESH RESULT: PASS csp=2

CSP Reaches Every Node Over CAN On Version 1
    [Documentation]    The same three legs with a four-byte header and CFP1 on
    ...                the wire. Every node on a link has to match, so this
    ...                reflashes both boards and rebuilds the ground node.
    [Tags]    physical    can    nucleo    csp    csp1    needs-can
    ${result}=    Run Process    ${CAN MESH}    --csp-version    1
    ...           stdout=${TEMPDIR}/can-mesh-1.out    stderr=STDOUT    timeout=1800s
    Log    ${result.stdout}
    Skip If    "CAN MESH RESULT: NOT RUN" in $result.stdout    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    CAN MESH RESULT: PASS csp=1
