*** Settings ***
Documentation    Firmware update, both routes.
...
...              An image reaches a node two ways: addressed to a reserved name
...              through the file transfer service, or block by block through
...              the direct upload path. Both end at the same update service,
...              so both are covered here rather than only the one that came
...              first.
Resource         resources/common.resource

*** Test Cases ***
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
