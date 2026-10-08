*** Settings ***
Library    Collections
Library    Process

*** Variables ***
${KFSW_REPO_DIR}      %{KFSW_REPO_DIR}
${KFSW_BUILD_ROOT}    %{KFSW_OUTPUT_ROOT=${KFSW_REPO_DIR}/../build}
${KFSW_PYTHON}        ${{__import__('sys').executable}}
# No numbered default: /dev/ttyACM0 is whichever board enumerated first, which
# on a bench with two of them is not the one under test. run.sh sets this from
# what preflight.sh discovered.
${DEBUG_SERIAL}       %{KFSW_DEBUG_SERIAL=}
${FTDI_DEVICE}        %{KFSW_FTDI_DEVICE=}
${HOLYBRO_RADIO}      %{KGROUND_HOLYBRO_DEVICE=}

*** Keywords ***
Run NUCLEO Boot Smoke
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/hil-smoke.sh
    ...    nucleo_l496zg
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
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
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    RETURN    ${result}

Run Holybro Raw Smoke
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/hil/radio-uhf/holybro/raw-nucleo-smoke.sh
    ...    --radio
    ...    ${HOLYBRO_RADIO}
    ...    --serial
    ...    ${DEBUG_SERIAL}
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    RETURN    ${result}

Run Holybro Beacon Smoke
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/hil/radio-uhf/holybro/beacon-smoke.sh
    ...    --radio
    ...    ${HOLYBRO_RADIO}
    ...    --serial
    ...    ${DEBUG_SERIAL}
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    RETURN    ${result}

Run Clock Smoke
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/hil/stm32/nucleo-l496zg/clock-smoke.sh
    ...    --serial
    ...    ${DEBUG_SERIAL}
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    RETURN    ${result}

Run Diagnostics Reset Smoke
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/hil/stm32/nucleo-l496zg/diagnostics-reset-smoke.sh
    ...    --serial
    ...    ${DEBUG_SERIAL}
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    RETURN    ${result}

Run Holybro CSP KISS Smoke
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/hil/radio-uhf/holybro/csp-kiss-smoke.sh
    ...    --radio
    ...    ${HOLYBRO_RADIO}
    ...    --serial
    ...    ${DEBUG_SERIAL}
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    RETURN    ${result}

Run FWU Over Radio
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/hil/fwu/fwu-over-radio.sh
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    ...    timeout=5400
    RETURN    ${result}

Run FWU Lite Smoke
    [Arguments]    @{options}
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/k-ground-fwu-lite-smoke.sh
    ...    @{options}
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    ...    timeout=900
    RETURN    ${result}

Run FWU FTP Route Smoke
    [Arguments]    @{options}
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/k-ground-fwu-ftp-smoke.sh
    ...    @{options}
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    ...    timeout=900
    RETURN    ${result}

Run Param Tables Smoke
    ${cached}=    Get Variable Value    ${PARAM_TABLES_RESULT}    ${NONE}
    IF    $cached is not None
        RETURN    ${cached}
    END
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/param-tables-smoke.sh
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    ...    timeout=120
    Set Suite Variable    ${PARAM_TABLES_RESULT}    ${result}
    RETURN    ${result}

Run Param Tables Smoke On Serial
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/tests/param-tables-smoke.sh
    ...    --serial
    ...    ${DEBUG_SERIAL}
    ...    stdout=${OUTPUT DIR}/${TEST NAME}.out
    ...    stderr=STDOUT
    ...    timeout=120
    RETURN    ${result}

HIL Command Should Pass
    [Arguments]    ${result}    ${success_marker}
    Log    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    ${success_marker}
