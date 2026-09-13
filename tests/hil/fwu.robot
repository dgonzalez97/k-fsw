*** Settings ***
Documentation    Firmware update, both routes.
...
...              An image reaches a node two ways: addressed to a reserved name
...              through the file transfer service, or block by block through
...              the direct upload path. Both end at the same update service,
...              so both are covered here rather than only the one that came
...              first.
Resource         resources/common.resource

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
    ${result}=    Run Process    python3
    ...    ${KFSW_REPO_DIR}/tests/hil/fwu/can-update.py
    ...    --ground    ${FWU_CAN_GROUND}
    ...    --image    ${FWU_CAN_IMAGE}
    ...    --revision    ${FWU_CAN_REVISION}
    ...    --serial    ${DEBUG_SERIAL}
    ...    --output    ${FWU_CAN_OUTPUT}
    ...    stdout=PIPE    stderr=STDOUT    timeout=1800
    HIL Command Should Pass    ${result}    CAN FWU RESULT: PASS

Direct Upload Carries An Image Between Two Nodes
    [Documentation]    Sends an image over CSP block by block and checks the
    ...    receiving node holds exactly what was sent: the byte count and the
    ...    checksum together, since either alone would pass a transfer that
    ...    lost a block and gained a duplicate.
    [Tags]    software    fwu    fwu-lite    csp
    ${result}=    Run FWU Lite Smoke
    HIL Command Should Pass    ${result}    K-GROUND FWU-LITE RESULT: PASS
    Should Contain    ${result.stdout}    blocks=105

Direct Upload Recovers From A Link That Drops Bytes
    [Documentation]    The same transfer over a bridge that deliberately loses
    ...    runs of bytes. A transport checksum discards what arrives damaged,
    ...    so a loss reaches the sender as silence rather than as a bad block,
    ...    and the sender has to notice for itself. Requires at least one block
    ...    to have been resent: a clean result would mean the losses never
    ...    reached the transfer and the recovery path is still untested.
    [Tags]    software    fwu    fwu-lite    csp    lossy
    ${result}=    Run FWU Lite Smoke    --lossy
    HIL Command Should Pass    ${result}    K-GROUND FWU-LITE RESULT: PASS
    Should Contain    ${result.stdout}    lossy=yes
    Should Not Contain    ${result.stdout}    resent=0

File Transfer Route Reaches The Update Service
    [Documentation]    An ordinary put addressed to the reserved name is
    ...    streamed into the update slot instead of being stored as a file.
    ...    The wire protocol is unchanged, so this is the existing transfer
    ...    with a different destination.
    [Tags]    software    fwu    ftp
    ${result}=    Run FWU FTP Route Smoke
    HIL Command Should Pass    ${result}    K-GROUND FWU-FTP RESULT: PASS
