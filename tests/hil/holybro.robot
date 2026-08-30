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
