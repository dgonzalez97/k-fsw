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
    Should Contain    ${out}    advancing sequences
    Should Contain    ${out}    silence for two intervals

Housekeeping Samples Survive In A File
    [Tags]    software    hk    storage
    ${out}=    Run Hosted Fixture    hk-store-smoke.sh    HK STORE SMOKE RESULT: PASS
    Should Contain    ${out}    the stored window can be read back
    Should Contain    ${out}    the ground cannot write under /hk
    Response Should Contain    ${out}    separate ground downloaded sequences 0..9 with values 42 and -7

The Ground Bridge Agrees With The Node About A Sample
    [Tags]    software    hk    bridge
    ${out}=    Run Hosted Fixture    hk-yamcs-smoke.sh    HK YAMCS BRIDGE SMOKE RESULT: PASS
    Should Contain    ${out}    the bridge and the shell read the same values
    Should Contain    ${out}    the frame is header plus values

The Ground Selects Classes And Receives Them In Priority Order
    [Tags]    software    hk    classes
    ${out}=    Run Hosted Fixture    hk-class-smoke.sh    HK CLASS SMOKE RESULT: PASS
    Response Should Contain    ${out}    ground received mask 0x42 replies: report/sequence 0/1, 0/0, 1/0; values 42, 42, -7
