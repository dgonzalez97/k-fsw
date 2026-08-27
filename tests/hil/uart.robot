*** Settings ***
Documentation    Physical UART/KISS, CSP, and debug-shell behavior.
Resource         resources/common.resource

*** Test Cases ***
Debug Shell And Physical UART CSP Link Succeed
    [Tags]    smoke    nucleo    shell    csp    uart    physical
    ${result}=    Run Physical UART CSP Smoke
    HIL Command Should Pass    ${result}    UART CSP HIL RESULT: PASS
    Should Contain    ${result.stdout}    K-FSW status
    Should Contain    ${result.stdout}    CSP ping 2: success
    Should Contain    ${result.stdout}    CSP ping 1: success
    Should Contain    ${result.stdout}    UART CSP test: PASS
    Should Contain    ${result.stdout}    mount_point: /kfsw
    Should Contain    ${result.stdout}    Storage test: PASS
    Should Contain    ${result.stdout}    destination=/hil/hil-4k.bin: PASS bytes=4096
    Should Contain    ${result.stdout}    destination=/hil/hil-16k.bin: PASS bytes=16384
    Should Contain    ${result.stdout}    second=/build/hil-16k-returned.bin: PASS
