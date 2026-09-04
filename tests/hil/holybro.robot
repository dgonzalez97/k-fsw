*** Settings ***
Documentation    Physical Holybro raw and CSP/KISS acceptance paths.
Resource         resources/common.resource

*** Test Cases ***
Holybro Raw Byte Link Succeeds
    [Tags]    physical    holybro    radio    raw
    Skip If    not $HOLYBRO_RADIO    KGROUND_HOLYBRO_DEVICE is not configured
    ${result}=    Run Holybro Raw Smoke
    HIL Command Should Pass    ${result}    HOLYBRO RAW/NUCLEO RESULT: PASS

Holybro CSP KISS Link Succeeds
    [Tags]    physical    holybro    radio    csp    uart
    Skip If    not $HOLYBRO_RADIO    KGROUND_HOLYBRO_DEVICE is not configured
    ${result}=    Run Holybro CSP KISS Smoke
    HIL Command Should Pass    ${result}    HOLYBRO CSP/KISS RESULT: PASS

Firmware Update Over The Radio Changes The Running Image
    [Documentation]    Uploads a second image over UHF and proves a different
    ...    one is running afterwards by asking the node who it is. Tens of
    ...    minutes: an application image over a few hundred bytes a second.
    [Tags]    physical    holybro    radio    fwu    slow
    Skip If    not $HOLYBRO_RADIO    KGROUND_HOLYBRO_DEVICE is not configured
    ${result}=    Run FWU Over Radio
    HIL Command Should Pass    ${result}    FWU RADIO RESULT: PASS
    Should Contain    ${result.stdout}    before=fwu-before
    Should Contain    ${result.stdout}    after=fwu-after
