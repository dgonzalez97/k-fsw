*** Settings ***
Documentation    Firmware batch checks; physical cases require explicit bench ports.
Resource         resources/common.resource

*** Variables ***
${BATCH_SHELL}    %{KFSW_DIAGNOSTICS_SHELL=}
${BATCH_KISS}     %{KFSW_DIAGNOSTICS_KISS=}
${BATCH_NODE}     %{KFSW_DIAGNOSTICS_NODE=2}
${BATCH_BAUD}     %{KFSW_DIAGNOSTICS_BAUD=115200}

*** Test Cases ***
Command Retries Execute Once Despite Lost Replies
    [Tags]    software    firmware-batches    command-retry
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/../.venv/bin/python    ${KFSW_REPO_DIR}/tests/command-retry-smoke.py
    ...    --executable    ${KFSW_REPO_DIR}/../build/linux/zephyr/zephyr.exe
    ...    --output    ${OUTPUT DIR}/command-retry-native
    ...    stderr=STDOUT    timeout=45
    HIL Command Should Pass    ${result}    COMMAND RETRY SMOKE RESULT: PASS

Board Rejects Duplicate Command Execution
    [Tags]    physical    firmware-batches    command-retry
    Skip If    not $BATCH_SHELL or not $BATCH_KISS    Set explicit shell and KISS devices.
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/../.venv/bin/python    ${KFSW_REPO_DIR}/tests/command-retry-smoke.py
    ...    --serial    ${BATCH_SHELL}    --kiss-device    ${BATCH_KISS}
    ...    --node    ${BATCH_NODE}    --baud    ${BATCH_BAUD}
    ...    --output    ${OUTPUT DIR}/command-retry-board
    ...    stderr=STDOUT    timeout=45
    HIL Command Should Pass    ${result}    COMMAND RETRY SMOKE RESULT: PASS
