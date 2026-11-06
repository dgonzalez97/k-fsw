*** Settings ***
Documentation    Ground operator requests over hosted PTY KISS; software evidence only.
Resource         resources/ground.resource

*** Test Cases ***
Ground Parameters And Remote Housekeeping Keep Their Reply Checks
    [Documentation]    PARAM writes, read-only and range validation; remote HK definition,
    ...    scheduled collection and missing-report refusal. Sample readback uses the flight console.
    [Tags]    software    ground    ground-services    param    hk
    Require Hosted Ground Dependencies
    ${result}=    Run Process    ${KFSW_PYTHON}    ${KFSW_REPO_DIR}/tests/ground-services-smoke.py
    ...    stdout=${OUTPUT DIR}/ground-services.out    stderr=STDOUT    timeout=180
    HIL Command Should Pass    ${result}    GROUND SERVICES RESULT: PASS

Silence Expires The Ground Watchdog And Reboots The Node
    [Documentation]    Coarse native_sim ticks accelerate the normal two-hour minimum.
    ...    The existing Ground Watchdog Accepts A Peer And Refuses Itself case covers feeds over KISS.
    [Tags]    software    ground    ground-services    gndwdt
    Run Hosted Fixture    gndwdt-expiry-smoke.sh    GNDWDT EXPIRY RESULT: PASS
