*** Settings ***
Documentation    Parameter tables: every table of the composition is registered
...              with its ID and band, and parameters are addressed by table and
...              offset. Hosted checks use native_sim console or PTY KISS, not wiring or RF.
...              The NUCLEO case uses its physical debug UART, not a CSP radio path.
Resource         resources/ground.resource

*** Test Cases ***
Every Core Table Is Registered On The Hosted Image
    [Documentation]    Lists the tables and checks the ID, band and name of each
    ...    core table.
    [Tags]    software    param    tables
    ${result}=    Run Param Tables Smoke
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
    Response Should Contain    ${result.stdout}    ${SPACE * 2}1${SPACE * 2}core${SPACE * 5}board
    Response Should Contain    ${result.stdout}    ${SPACE}25${SPACE * 2}service${SPACE * 2}log

One Offset Repeats Across Tables
    [Documentation]    Checks parameters at offset zero in several tables.
    [Tags]    software    param    tables
    ${result}=    Run Param Tables Smoke
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
    Response Should Contain    ${result.stdout}    board${SPACE * 7}0x00${SPACE * 2}node_id
    Response Should Contain    ${result.stdout}    telemetry${SPACE * 3}0x00${SPACE * 2}uptime_s
    Response Should Contain    ${result.stdout}    log${SPACE * 9}0x00${SPACE * 2}log_level

The Listing Reports Write Behaviour
    [Documentation]    Checks the mode column, which comes from each definition.
    [Tags]    software    param    tables
    ${result}=    Run Param Tables Smoke
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
    Response Should Contain    ${result.stdout}    uptime_s${SPACE * 26}u32${SPACE * 5}r
    # Kept across a reset and read at the next start: wpb.
    Response Should Contain    ${result.stdout}    boot_delay_ms${SPACE * 21}u16${SPACE * 5}wpb
    Response Should Contain    ${result.stdout}    app_report_ms${SPACE * 21}u16${SPACE * 5}wp

NUCLEO Reports Its Tables Over The Debug UART
    [Documentation]    The same listing read from a board.
    [Tags]    param    tables    nucleo    physical    needs-nucleo
    ${result}=    Run Param Tables Smoke On Serial
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS

Remote Tables Describe Their Layer And Contents
    [Documentation]    k-ground requests flight descriptors over PTY KISS; no physical UART evidence.
    [Tags]    software    param    tables    ground
    Require Hosted Ground Dependencies
    Open Ground Pair    ${OUTPUT DIR}/ground-tables
    TRY
        ${out}=    Ground Command    param whichtables 1    Uploaded table files, adoptions and reverts
        Should Match Regexp    ${out}    id +layer +name +params +holds
        Should Match Regexp    ${out}    (?<![0-9])37 +service .*Uploaded table files, adoptions and reverts
    FINALLY
        Close Ground Pair
    END

One Remote Table Fits The Slowest Link
    [Documentation]    k-ground asks for one table of descriptors over a relay paced
    ...    at 4800 baud, the slowest link this has to work on. A whole node does not
    ...    fit the refresh budget at that rate; one table does. No physical UART evidence.
    [Tags]    software    param    tables    ground
    Require Hosted Ground Dependencies
    Open Ground Pair    ${OUTPUT DIR}/ground-slow-table    baud=4800
    TRY
        ${out}=    Ground Command    param table 1 4    route_table    timeout=60
        Should Match Regexp    ${out}    4 +0x00 +tx_packets
        Should Match Regexp    ${out}    4 +0x14 +interfaces
    FINALLY
        Close Ground Pair
    END

Hosted Boot Diagnostics Are Explicitly Unavailable Remotely
    [Documentation]    k-ground reads hosted diagnostics over PTY KISS; u8 zero is false.
    ...    No MCUboot or reset evidence.
    [Tags]    software    boot    param    ground
    Require Hosted Ground Dependencies
    Open Ground Pair    ${OUTPUT DIR}/ground-boot-diagnostics
    TRY
        Ground Command    param get 1 boot_attempts    1:boot_attempts = 4294967295
        Ground Command    param get 1 boot_revert_reason    1:boot_revert_reason = 255
        Ground Command    param get 1 boot_trial_valid    1:boot_trial_valid = 0
    FINALLY
        Close Ground Pair
    END

Remote Parameter Read Recovers After A Lost Reply
    [Documentation]    Drop one value reply over PTY KISS and check the caller's
    ...    resend counter as well as the returned value. No physical link evidence.
    [Tags]    software    param    tables    ground    lossy
    [Timeout]    360 seconds
    Require Hosted Ground Dependencies
    Open Ground Pair    ${OUTPUT DIR}/ground-param-retry    drop_param_reply=${TRUE}
    TRY
        ${out}=    Ground Command    param get param_read_resends    param_read_resends =
        ${before}=    Evaluate    int(__import__('re').search(r'param_read_resends = ([0-9]+)', $out)[1])
        Ground Command    param get 1 node_id    1:node_id = 1
        ${out}=    Ground Command    param get param_read_resends    param_read_resends =
        ${after}=    Evaluate    int(__import__('re').search(r'param_read_resends = ([0-9]+)', $out)[1])
        Should Be True    ${after} > ${before}
        ${stats}=    Ground Link Statistics
        Should Be Equal As Integers    ${stats}[param_reply_dropped]    1
        Should Be True    ${stats}[param_pulls] >= 2
        Log    param_read_resends: ${before} -> ${after}    console=yes
    FINALLY
        Close Ground Pair
    END
