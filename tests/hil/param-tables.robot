*** Settings ***
Documentation    Parameter tables: every table of the composition is registered
...              with its ID and band, and parameters are addressed by table and
...              offset. The NUCLEO case runs the same check over the debug UART.
Resource         resources/common.resource

*** Test Cases ***
Every Core Table Is Registered On The Hosted Image
    [Documentation]    Lists the tables and checks the ID, band and name of each
    ...    core table.
    [Tags]    software    param    tables
    ${result}=    Run Param Tables Smoke
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
    Should Contain    ${result.stdout}      1  core     board
    Should Contain    ${result.stdout}     25  service  log

One Offset Repeats Across Tables
    [Documentation]    Checks parameters at offset zero in several tables.
    [Tags]    software    param    tables
    ${result}=    Run Param Tables Smoke
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
    Should Contain    ${result.stdout}    board       0x00  node_id
    Should Contain    ${result.stdout}    telemetry   0x00  uptime_s
    Should Contain    ${result.stdout}    log         0x00  log_level

The Listing Reports Write Behaviour
    [Documentation]    Checks the mode column, which comes from each definition.
    [Tags]    software    param    tables
    ${result}=    Run Param Tables Smoke
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
    Should Contain    ${result.stdout}    uptime_s                          u32     r
    # Kept across a reset and read at the next start: wpb.
    Should Contain    ${result.stdout}    boot_delay_ms                     u16     wpb
    Should Contain    ${result.stdout}    app_report_ms                     u16     wp

NUCLEO Reports Its Tables Over The Debug UART
    [Documentation]    The same listing read from a board.
    [Tags]    param    tables    nucleo    physical
    ${result}=    Run Param Tables Smoke On Serial
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
