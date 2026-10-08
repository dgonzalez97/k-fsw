*** Settings ***
Documentation    Physical UART/KISS, CSP, and debug-shell behavior.
...              This does not prove a radio path.
Resource         resources/common.resource

*** Test Cases ***
Debug Shell And Physical UART CSP Link Succeed
    [Tags]    smoke    nucleo    shell    csp    uart    physical
    ${result}=    Run Physical UART CSP Smoke
    HIL Command Should Pass    ${result}    UART CSP HIL RESULT: PASS
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
