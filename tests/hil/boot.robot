*** Settings ***
Documentation    NUCLEO-L496ZG boot and readiness behavior.
Resource         resources/common.resource

*** Test Cases ***
NUCLEO Reports Boot And Ready
    [Tags]    smoke    nucleo    physical
    ${result}=    Run NUCLEO Boot Smoke
    HIL Command Should Pass    ${result}    HIL RESULT: PASS
    Should Contain    ${result.stdout}    @BOOT
    Should Contain    ${result.stdout}    @READY
