*** Settings ***
Documentation    Boot readiness over native_sim console or NUCLEO physical debug UART.
...              Hosted LittleFS checks do not prove silicon reset behavior.
...              UART readiness does not prove CSP links or RF delivery.
Resource         resources/common.resource

*** Test Cases ***
NUCLEO Reports Boot And Ready
    [Tags]    smoke    nucleo    physical
    ${result}=    Run NUCLEO Boot Smoke
    HIL Command Should Pass    ${result}    HIL RESULT: PASS
    Should Contain    ${result.stdout}    @BOOT
    Should Contain    ${result.stdout}    @READY

Hosted Image Reports Boot And Ready
    [Documentation]    Hosted LittleFS boot markers and table 32; no silicon reset evidence.
    [Tags]    software    boot
    ${result}=    Run Process    ${KFSW_PYTHON}    ${KFSW_REPO_DIR}/tests/hosted-boot-smoke.py
    ...    --executable    ${KFSW_BUILD_ROOT}/linux/zephyr/zephyr.exe
    ...    --output    ${OUTPUT DIR}/hosted-boot    stderr=STDOUT    timeout=45
    HIL Command Should Pass    ${result}    HOSTED BOOT RESULT: PASS
    Response Should Contain    ${result.stdout}    markers ordered; table 32 readable

Restart Count Advances On A Hosted Restart
    [Documentation]    Re-execution with one flash file proves the saved counter, not reset cause.
    [Tags]    software    boot    persistence
    ${result}=    Run Process    ${KFSW_PYTHON}    ${KFSW_REPO_DIR}/tests/hosted-boot-smoke.py
    ...    --executable    ${KFSW_BUILD_ROOT}/linux/zephyr/zephyr.exe
    ...    --output    ${OUTPUT DIR}/hosted-restart    --restart
    ...    stderr=STDOUT    timeout=45
    HIL Command Should Pass    ${result}    HOSTED BOOT RESULT: PASS
    Should Contain    ${result.stdout}    boot_count advanced
