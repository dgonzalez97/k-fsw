*** Settings ***
Documentation    Physical UART/KISS, CSP, and debug-shell behavior.
...              This does not prove a radio path.
Resource         resources/common.resource

*** Test Cases ***
Debug Shell And Physical UART CSP Link Succeed
    [Tags]    smoke    nucleo    shell    csp    uart    physical    needs-ftdi
    ${result}=    Run Physical UART CSP Smoke
    Skip If    "UART CSP HIL RESULT: NOT RUN" in $result.stdout
    ...    ${result.stdout}
    HIL Command Should Pass    ${result}    UART CSP HIL RESULT: PASS
    Should Contain    ${result.stdout}    CSP DEBUG RDP: PASS levels=0,1,2 refused=256,-1,text restored=yes
    Should Contain    ${result.stdout}    CSP DEBUG PACKET: PASS values=0,1 refused=2 restored=yes
    Should Contain    ${result.stdout}    K-FSW status
    Response Should Contain    ${result.stdout}    CSP ping 2: success
    Response Should Contain    ${result.stdout}    CSP ping 1: success
    Should Contain    ${result.stdout}    UART CSP test: PASS
    Should Contain    ${result.stdout}    mount_point: /kfsw
    Should Contain    ${result.stdout}    Storage test: PASS
    Response Should Contain    ${result.stdout}    -> /hil/hil-4k.bin: PASS
    Response Should Contain    ${result.stdout}    bytes: 4096
    Response Should Contain    ${result.stdout}    -> /hil/hil-16k.bin: PASS
    Response Should Contain    ${result.stdout}    bytes: 16384
    Response Should Contain    ${result.stdout}    /build/hil-16k-returned.bin: PASS

F746 Physical UART CSP Link Succeeds
    [Tags]    smoke    physical    uart    csp    needs-ftdi    needs-f746
    ${result}=    Run Physical UART CSP Smoke    nucleo_f746zg
    Skip If    "UART CSP HIL RESULT: NOT RUN" in $result.stdout
    ...    ${result.stdout}
    HIL Command Should Pass    ${result}    UART CSP HIL RESULT: PASS
    Should Contain    ${result.stdout}    CSP DEBUG RDP: PASS levels=0,1,2 refused=256,-1,text restored=yes
    Should Contain    ${result.stdout}    CSP DEBUG PACKET: PASS values=0,1 refused=2 restored=yes
    Response Should Contain    ${result.stdout}    CSP ping 3: success
    Response Should Contain    ${result.stdout}    CSP ping 1: success
    Response Should Contain    ${result.stdout}    3:node_id = 3
    Should Contain    ${result.stdout}    SKIP: storage info, storage test, mount point and FTP transfers
