*** Settings ***
Documentation    HK capture/replay and CSP 2 discovery/interface diagnostics.
Resource         resources/common.resource

*** Variables ***
${DIAGNOSTICS_PYTHON}    ${KFSW_PYTHON}
${DIAGNOSTICS_SCRIPT}    ${KFSW_REPO_DIR}/tests/diagnostics-smoke.py
${CSP_TOOL}              %{KFSW_CSP_TOOLS_TEST_BINARY=}
${BENCH_SHELL}           %{KFSW_DIAGNOSTICS_SHELL=}
${BENCH_KISS}            %{KFSW_DIAGNOSTICS_KISS=}
${BENCH_NODE}            %{KFSW_DIAGNOSTICS_NODE=2}
${BENCH_BAUD}            %{KFSW_DIAGNOSTICS_BAUD=115200}

*** Test Cases ***
Linux Preserves Both HK Reports And Replays Their Bytes
    [Tags]    software    diagnostics    hk
    ${result}=    Run Process
    ...    ${DIAGNOSTICS_PYTHON}    ${DIAGNOSTICS_SCRIPT}
    ...    --executable    ${KFSW_REPO_DIR}/../build/linux/zephyr/zephyr.exe
    ...    --output    ${OUTPUT DIR}/diagnostics-native
    ...    stderr=STDOUT    timeout=60
    HIL Command Should Pass    ${result}    DIAGNOSTICS SMOKE RESULT: PASS

CSP Tools Diagnose A Linux Node Through KISS
    [Tags]    software    diagnostics    csp    host-tools    discover
    Skip If    not $CSP_TOOL    Set KFSW_CSP_TOOLS_TEST_BINARY to the built csp-kiss binary.
    ${result}=    Run Process
    ...    ${DIAGNOSTICS_PYTHON}    ${DIAGNOSTICS_SCRIPT}
    ...    --executable    ${KFSW_REPO_DIR}/../build/linux/zephyr/zephyr.exe
    ...    --csp-tool    ${CSP_TOOL}
    ...    --output    ${OUTPUT DIR}/diagnostics-tools
    ...    stderr=STDOUT    timeout=60
    HIL Command Should Pass    ${result}    DIAGNOSTICS SMOKE RESULT: PASS

Board Telemetry Can Be Captured And Replayed
    [Tags]    physical    diagnostics    hk    csp    discover
    Skip If    not $BENCH_SHELL or not $BENCH_KISS or not $CSP_TOOL
    ...    Set KFSW_DIAGNOSTICS_SHELL, KFSW_DIAGNOSTICS_KISS and KFSW_CSP_TOOLS_TEST_BINARY.
    ${result}=    Run Process
    ...    ${DIAGNOSTICS_PYTHON}    ${DIAGNOSTICS_SCRIPT}
    ...    --serial    ${BENCH_SHELL}    --kiss-device    ${BENCH_KISS}
    ...    --node    ${BENCH_NODE}    --baud    ${BENCH_BAUD}
    ...    --csp-tool    ${CSP_TOOL}
    ...    --output    ${OUTPUT DIR}/diagnostics-board
    ...    stderr=STDOUT    timeout=60
    HIL Command Should Pass    ${result}    DIAGNOSTICS SMOKE RESULT: PASS
