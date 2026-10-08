*** Settings ***
Documentation    Hosted k-ground and flight nodes use CSP over PTY KISS.
...              These exchanges prove software routing and replies, not UART wiring or RF.
Resource         resources/ground.resource
Test Setup       Require Hosted Ground Dependencies
Test Teardown    Close Ground Pair

*** Test Cases ***
The Ground Pair Comes Up With Its Roles And Routes
    [Tags]    software    ground    csp
    ${out}=    Run Hosted Fixture    k-ground-csp-smoke.sh    K-GROUND CSP RESULT: PASS
    Response Should Contain    ${out}    CSP node: 16
    Response Should Contain    ${out}    19/14 -> KISS direct
    Should Contain    ${out}    implementation: holybro-sik
    Response Should Contain    ${out}    CSP node: 19
    Response Should Contain    ${out}    node 19 UHF module: absent

Ground Watchdog Accepts A Peer And Refuses Itself
    [Documentation]    The hosted flight peer arms its watchdog; ground feeds it over PTY KISS.
    [Tags]    software    ground    gndwdt
    Open Ground Pair    ${OUTPUT DIR}/ground-watchdog
    Ground Command    gndwdt feed 1    fed: yes
    ${remote}=    Ground Command    gndwdt show 1    ground_wtd_timeout: 86400
    Response Should Contain    ${remote}    node: 1
    Should Match Regexp    ${remote}    ground_wtd_cnt: [1-9][0-9]*
    ${out}=    Flight Command    gndwdt show    last_node: 19
    Response Should Contain    ${out}    contacts: 1
    Should Contain    ${out}    state: armed
    Ground Command    gndwdt feed 19    Node 19 is this node

A File Crosses The Ground Pair Both Ways
    [Tags]    software    ground    ftp
    ${out}=    Run Hosted Fixture    k-ground-ftp-smoke.sh    K-GROUND FTP RESULT: PASS
    Should Match Regexp    ${out}    crc32=[0-9a-f]{8} bytes=256

A File Crosses A Link That Drops Bytes
    [Tags]    software    ground    ftp    lossy
    ${out}=    Run Hosted Fixture    k-ground-ftp-smoke.sh    K-GROUND FTP RESULT: PASS    --lossy
    Should Match Regexp    ${out}    crc32=[0-9a-f]{8} bytes=32768 lossy=yes dropped=[1-9][0-9]* resent=[1-9][0-9]*

Remote Log Reads As Text And As A Dictionary
    [Tags]    software    ground    log    journal
    Open Ground Pair    ${OUTPUT DIR}/ground-log
    Ground Command    param set 1 log_remote_format 0    1:log_remote_format = 0
    ${text}=    Ground Command    log remote 1 4    format: text
    Should Not Contain    ${text}    pkg=
    Should Match Regexp    ${text}    [0-9]+ t=[0-9]+ms
    ${journal_text}=    Wait Until Keyword Succeeds    10s    250ms
    ...    Ground Command    journal remote 1 4    seq=    1
    Ground Command    param set 1 log_remote_format 1    1:log_remote_format = 1
    ${dictionary}=    Ground Command    log remote 1 4    format: dictionary
    Should Contain    ${dictionary}    pkg=
    ${rc}    ${decoded}=    Decode Ground Capture    ${dictionary}
    Should Be Equal As Integers    ${rc}    0
    Should Not Contain    ${decoded}    pkg=
    ${text_records}=    Evaluate    dict(__import__('re').findall(r'([0-9]+) t=[0-9]+ms (.*)', $text))
    ${decoded_records}=    Evaluate    dict(__import__('re').findall(r'([0-9]+) t=[0-9]+ms (.*)', $decoded))
    ${common}=    Evaluate    set($text_records) & set($decoded_records)
    ${count}=    Get Length    ${common}
    Should Be True    ${count} >= 2
    FOR    ${sequence}    IN    @{common}
        Should Be Equal    ${text_records}[${sequence}]    ${decoded_records}[${sequence}]
    END
    # Journal events retain their structured form under either log format.
    ${journal_dictionary}=    Ground Command    journal remote 1 4    seq=
    ${events_text}=    Evaluate    __import__('re').findall(r'seq=.*', $journal_text)
    ${events_dictionary}=    Evaluate    __import__('re').findall(r'seq=.*', $journal_dictionary)
    Should Be Equal    ${events_text}    ${events_dictionary}

Decoder Rejects A Mismatched Rendered Message
    [Documentation]    CRC32 checks agreement with the node-rendered text, not ELF identity.
    ...    Different text from a mismatched image is not decoded and exits nonzero.
    ...    Another ELF rendering byte-identical text, or a CRC32 collision, can pass.
    [Tags]    software    ground    log
    Open Ground Pair    ${OUTPUT DIR}/ground-wrong-image
    Ground Command    param set 1 log_remote_format 1    1:log_remote_format = 1
    ${capture}=    Ground Command    log remote 1 4    pkg=
    ${rc}    ${decoded}=    Decode Ground Capture    ${capture}
    Should Be Equal As Integers    ${rc}    0
    Should Not Contain    ${decoded}    pkg=
    ${rc}    ${decoded}=    Decode Ground Capture    ${capture}    ${TRUE}
    Should Be Equal As Integers    ${rc}    1
    Should Contain    ${decoded}    rendered-text CRC32 mismatch
    Should Contain    ${decoded}    pkg=

CSP Services Keep Their Remote Regression Checks
    [Documentation]    Two hosted nodes over PTY KISS; no physical UART evidence.
    [Tags]    software    csp    param    ftp    log
    Run Hosted Fixture    csp-smoke.sh    CSP RESULT: PASS
