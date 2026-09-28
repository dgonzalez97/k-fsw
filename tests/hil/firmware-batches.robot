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
    ...    ${KFSW_PYTHON}    ${KFSW_REPO_DIR}/tests/command-retry-smoke.py
    ...    --executable    ${KFSW_REPO_DIR}/../build/linux/zephyr/zephyr.exe
    ...    --output    ${OUTPUT DIR}/command-retry-native
    ...    stderr=STDOUT    timeout=45
    HIL Command Should Pass    ${result}    COMMAND RETRY SMOKE RESULT: PASS

Board Rejects Duplicate Command Execution
    [Tags]    physical    firmware-batches    command-retry
    Skip If    not $BATCH_SHELL or not $BATCH_KISS    Set explicit shell and KISS devices.
    ${result}=    Run Process
    ...    ${KFSW_PYTHON}    ${KFSW_REPO_DIR}/tests/command-retry-smoke.py
    ...    --serial    ${BATCH_SHELL}    --kiss-device    ${BATCH_KISS}
    ...    --node    ${BATCH_NODE}    --baud    ${BATCH_BAUD}
    ...    --output    ${OUTPUT DIR}/command-retry-board
    ...    stderr=STDOUT    timeout=45
    HIL Command Should Pass    ${result}    COMMAND RETRY SMOKE RESULT: PASS

UTC Procedures Run And Cancel On Linux
    [Tags]    software    firmware-batches    fbo-utc
    ${result}=    Run Process
    ...    ${KFSW_PYTHON}    ${KFSW_REPO_DIR}/tests/fbo-utc-smoke.py
    ...    --executable    ${KFSW_REPO_DIR}/../build/linux/zephyr/zephyr.exe
    ...    --output    ${OUTPUT DIR}/fbo-utc-native
    ...    stderr=STDOUT    timeout=45
    HIL Command Should Pass    ${result}    FBO UTC SMOKE RESULT: PASS

UTC Procedures Run And Cancel On A Dedicated Board
    [Tags]    physical    firmware-batches    fbo-utc
    Skip If    not $BATCH_SHELL    Set an explicit dedicated bench console.
    ${result}=    Run Process
    ...    ${KFSW_PYTHON}    ${KFSW_REPO_DIR}/tests/fbo-utc-smoke.py
    ...    --serial    ${BATCH_SHELL}    --output    ${OUTPUT DIR}/fbo-utc-board
    ...    stderr=STDOUT    timeout=45
    HIL Command Should Pass    ${result}    FBO UTC SMOKE RESULT: PASS

Journal Retains Events Across Linux Process Restarts
    [Tags]    software    firmware-batches    journal
    ${result}=    Run Process
    ...    ${KFSW_PYTHON}    ${KFSW_REPO_DIR}/tests/journal-smoke.py
    ...    --executable    ${KFSW_REPO_DIR}/../build/linux/zephyr/zephyr.exe
    ...    --output    ${OUTPUT DIR}/journal-native
    ...    stderr=STDOUT    timeout=45
    HIL Command Should Pass    ${result}    JOURNAL SMOKE RESULT: PASS

Board Commits And Returns An Important Event
    [Tags]    physical    firmware-batches    journal
    Skip If    not $BATCH_SHELL or not $BATCH_KISS    Set explicit shell and KISS devices.
    ${result}=    Run Process
    ...    ${KFSW_PYTHON}    ${KFSW_REPO_DIR}/tests/journal-smoke.py
    ...    --serial    ${BATCH_SHELL}    --kiss-device    ${BATCH_KISS}
    ...    --node    ${BATCH_NODE}    --baud    ${BATCH_BAUD}
    ...    --output    ${OUTPUT DIR}/journal-board
    ...    stderr=STDOUT    timeout=45
    HIL Command Should Pass    ${result}    JOURNAL SMOKE RESULT: PASS
