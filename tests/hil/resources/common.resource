*** Settings ***
Library    Collections
Library    Process

*** Variables ***
${KFSW_REPO_DIR}      %{KFSW_REPO_DIR}
${DEBUG_SERIAL}       %{KFSW_DEBUG_SERIAL=/dev/ttyACM0}
${FTDI_DEVICE}        %{KFSW_FTDI_DEVICE=}
${HOLYBRO_RADIO}      %{KGROUND_HOLYBRO_DEVICE=}

*** Keywords ***
Run NUCLEO Boot Smoke
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/hil-smoke.sh
    ...    nucleo_l496zg
    ...    stdout=PIPE
    ...    stderr=STDOUT
    ...    env:KFSW_SERIAL=${DEBUG_SERIAL}
    RETURN    ${result}

Run Physical UART CSP Smoke
    @{arguments}=    Create List    --serial    ${DEBUG_SERIAL}
    IF    $FTDI_DEVICE
        Append To List    ${arguments}    --ftdi    ${FTDI_DEVICE}
    END
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/uart-csp-smoke.sh
    ...    @{arguments}
    ...    stdout=PIPE
    ...    stderr=STDOUT
    RETURN    ${result}

Run Holybro Raw Smoke
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/hil/radio-uhf/holybro/raw-smoke.sh
    ...    --device
    ...    ${HOLYBRO_RADIO}
    ...    stdout=PIPE
    ...    stderr=STDOUT
    RETURN    ${result}

Run Holybro CSP KISS Smoke
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/hil/radio-uhf/holybro/csp-kiss-smoke.sh
    ...    --radio
    ...    ${HOLYBRO_RADIO}
    ...    --serial
    ...    ${DEBUG_SERIAL}
    ...    stdout=PIPE
    ...    stderr=STDOUT
    RETURN    ${result}

HIL Command Should Pass
    [Arguments]    ${result}    ${success_marker}
    Log    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    ${success_marker}
