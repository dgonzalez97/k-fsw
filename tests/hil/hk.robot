*** Settings ***
Documentation    Hosted housekeeping uses LittleFS and CSP over uart_1 PTY KISS.
...              A listening ground proves unprompted software delivery, not RF or UART wiring.
Resource         resources/ground.resource
Test Setup       Require Hosted Ground Dependencies

*** Test Cases ***
A Node Beacons And A Listening Ground Hears It
    [Tags]    software    hk    beacon
    ${out}=    Run Hosted Fixture    hk-beacon-smoke.sh    HK BEACON SMOKE RESULT: PASS
    Should Contain    ${out}    an interval under the floor is refused
    Should Contain    ${out}    the beacon is header plus the report's values
    Should Contain    ${out}    the node counted what it sent
    Should Contain    ${out}    beacons keep coming

Housekeeping Samples Survive In A File
    [Tags]    software    hk    storage
    ${out}=    Run Hosted Fixture    hk-store-smoke.sh    HK STORE SMOKE RESULT: PASS
    Should Contain    ${out}    the stored window can be read back
    Should Contain    ${out}    the ground cannot write under /hk

The Ground Bridge Agrees With The Node About A Sample
    [Tags]    software    hk    bridge
    ${out}=    Run Hosted Fixture    hk-yamcs-smoke.sh    HK YAMCS BRIDGE SMOKE RESULT: PASS
    Should Contain    ${out}    the bridge and the shell read the same values
    Should Contain    ${out}    the frame is header plus values
