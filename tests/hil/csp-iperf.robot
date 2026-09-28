*** Settings ***
Documentation    Bounded CSP echo benchmark; physical execution requires an explicit device.
Resource         resources/common.resource

*** Variables ***
${IPERF_TOOL}    %{KFSW_CSP_IPERF_TEST_BINARY=}
${IPERF_DEVICE}  %{KFSW_DIAGNOSTICS_KISS=}
${IPERF_NODE}    %{KFSW_DIAGNOSTICS_NODE=2}
${IPERF_BAUD}    %{KFSW_DIAGNOSTICS_BAUD=115200}
${IPERF_SOURCE}  %{KFSW_IPERF_SOURCE=30}

*** Test Cases ***
CSP Benchmark Validates Linux Echo Replies
    [Tags]    software    diagnostics    csp    iperf
    Skip If    not $IPERF_TOOL    Set KFSW_CSP_IPERF_TEST_BINARY to the built csp-iperf binary.
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/../.venv/bin/python    ${KFSW_REPO_DIR}/tests/csp-iperf-smoke.py
    ...    --executable    ${KFSW_REPO_DIR}/../build/linux/zephyr/zephyr.exe
    ...    --tool    ${IPERF_TOOL}    --output    ${OUTPUT DIR}/iperf-native
    ...    stderr=STDOUT    timeout=30
    HIL Command Should Pass    ${result}    CSP IPERF SMOKE RESULT: PASS

CSP Benchmark Validates Bench Echo Replies
    [Tags]    physical    diagnostics    csp    iperf
    Skip If    not $IPERF_TOOL or not $IPERF_DEVICE
    ...    Set KFSW_CSP_IPERF_TEST_BINARY and KFSW_DIAGNOSTICS_KISS explicitly.
    ${result}=    Run Process
    ...    ${KFSW_REPO_DIR}/../.venv/bin/python    ${KFSW_REPO_DIR}/tests/csp-iperf-smoke.py
    ...    --device    ${IPERF_DEVICE}    --baud    ${IPERF_BAUD}
    ...    --node    ${IPERF_NODE}    --source    ${IPERF_SOURCE}
    ...    --tool    ${IPERF_TOOL}    --output    ${OUTPUT DIR}/iperf-bench
    ...    stderr=STDOUT    timeout=30
    HIL Command Should Pass    ${result}    CSP IPERF SMOKE RESULT: PASS
