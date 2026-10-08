*** Settings ***
Documentation    Firmware update over both routes: a put to the reserved name
...              through file transfer, and block by block through FWU lite.
...              Hosted cases use PTY KISS, proving software exchange, not flash swaps or RF.
...              The physical CAN case checks the wired bus and slots, not a radio path.
Resource         resources/ground.resource

*** Variables ***
${FWU_CAN_GROUND}      %{KFSW_FWU_CAN_GROUND=}
${FWU_CAN_IMAGE}       %{KFSW_FWU_CAN_IMAGE=}
${FWU_CAN_REVISION}    %{KFSW_FWU_CAN_REVISION=fwu-can-after}
${FWU_CAN_OUTPUT}      %{KFSW_FWU_CAN_OUTPUT=/tmp/kfsw-fwu-can}

*** Test Cases ***
CAN Update Keeps Both Images Readable
    [Documentation]    Uploads through FTP and FWU lite, compares slot files,
    ...    reverts a trial, then confirms and reboots the candidate.
    [Tags]    physical    nucleo    can    fwu    ftp
    Skip If    not $FWU_CAN_GROUND or not $FWU_CAN_IMAGE    Prebuilt CAN images not configured
    # The interpreter matters: the fixture spawns a helper that needs littlefs,
    # which only the workspace environment has.
    ${result}=    Run Process    ${KFSW_PYTHON}
    ...    ${KFSW_REPO_DIR}/tests/hil/fwu/can-update.py
    ...    --ground    ${FWU_CAN_GROUND}
    ...    --image    ${FWU_CAN_IMAGE}
    ...    --revision    ${FWU_CAN_REVISION}
    ...    --serial    ${DEBUG_SERIAL}
    ...    --output    ${FWU_CAN_OUTPUT}
    ...    stdout=PIPE    stderr=STDOUT    timeout=1800
    HIL Command Should Pass    ${result}    CAN FWU RESULT: PASS

Direct Upload Carries An Image Between Two Nodes
    [Documentation]    Sends an image over CSP block by block and checks that the
    ...    receiving node has the same byte count and checksum.
    [Tags]    software    fwu    fwu-lite    csp
    Require Hosted Ground Dependencies
    ${result}=    Run FWU Lite Smoke
    HIL Command Should Pass    ${result}    K-GROUND FWU-LITE RESULT: PASS
    Should Match Regexp    ${result.stdout}    crc32=[0-9a-f]{8} bytes=20000(?![0-9]) blocks=105(?![0-9]) resent=[0-9]+

Direct Upload Recovers From A Link That Drops Bytes
    [Documentation]    The same transfer over a bridge that drops runs of bytes.
    ...    At least one block must be resent.
    [Tags]    software    fwu    fwu-lite    csp    lossy
    Require Hosted Ground Dependencies
    ${result}=    Run FWU Lite Smoke    --lossy
    HIL Command Should Pass    ${result}    K-GROUND FWU-LITE RESULT: PASS
    Should Match Regexp    ${result.stdout}    crc32=[0-9a-f]{8} bytes=20000(?![0-9]) blocks=105(?![0-9]) lossy=yes resent=[1-9][0-9]*
    Should Not Contain    ${result.stdout}    resent=0

File Transfer Route Reaches The Update Service
    [Documentation]    A put to the reserved name goes to the update slot
    ...    instead of a file.
    [Tags]    software    fwu    ftp
    Require Hosted Ground Dependencies
    ${result}=    Run FWU FTP Route Smoke
    HIL Command Should Pass    ${result}    K-GROUND FWU-FTP RESULT: PASS
    Should Match Regexp    ${result.stdout}    crc32=[0-9a-f]{8} bytes=20000(?![0-9]) blocks=105(?![0-9]) resent=[0-9]+

File Transfer Update Recovers From A Link That Drops Bytes
    [Documentation]    FTP retries preserve the image over a PTY bridge dropping bytes.
    [Tags]    software    fwu    ftp    lossy
    Require Hosted Ground Dependencies
    ${result}=    Run FWU FTP Route Smoke    --lossy
    HIL Command Should Pass    ${result}    K-GROUND FWU-FTP RESULT: PASS
    Should Match Regexp    ${result.stdout}    crc32=[0-9a-f]{8} bytes=20000(?![0-9]) blocks=105(?![0-9]) lossy=yes resent=[1-9][0-9]*
