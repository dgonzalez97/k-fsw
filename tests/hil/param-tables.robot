*** Settings ***
Documentation    Parameter tables: every table a composition declares is there.
...
...              Deliberately about existence and addressing rather than
...              content. A value is only as good as the layer underneath it,
...              so asserting a particular figure here would test that layer
...              instead of the table scheme. What has to hold is that each
...              table registers under the identifier its owner was allocated,
...              in the band that owner belongs to, and that every parameter is
...              addressed by table and offset.
...
...              The hosted case runs everywhere. The NUCLEO case is the same
...              check over a board's debug UART, where the tables are built
...              from real hardware rather than a simulated one.
Resource         resources/common.resource

*** Test Cases ***
Every Core Table Is Registered On The Hosted Image
    [Documentation]    Lists the tables and checks each core table appears
    ...    under its own identifier and band. A table registered under the
    ...    wrong number would still print its name, and the number is what the
    ...    wire uses, so both are checked together.
    [Tags]    software    param    tables
    ${result}=    Run Param Tables Smoke
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
    Should Contain    ${result.stdout}      1  core     board
    Should Contain    ${result.stdout}     25  service  log

One Offset Repeats Across Tables
    [Documentation]    Each table starts its own address space at zero, which
    ...    is what the scheme exists for. Six parameters at offset zero in six
    ...    different tables would have collided in the flat identifier space
    ...    this replaced.
    [Tags]    software    param    tables
    ${result}=    Run Param Tables Smoke
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
    Should Contain    ${result.stdout}    board       0x00  node_id
    Should Contain    ${result.stdout}    telemetry   0x00  uptime_s
    Should Contain    ${result.stdout}    log         0x00  log_level

The Listing Reports Write Behaviour
    [Documentation]    The mode column is derived from the definition rather
    ...    than written by hand, so a parameter whose behaviour drifts from its
    ...    documented contract shows up here. A stored value reported as live
    ...    is the failure the whole scheme exists to prevent.
    [Tags]    software    param    tables
    ${result}=    Run Param Tables Smoke
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
    Should Contain    ${result.stdout}    uptime_s                          u32     r
    Should Contain    ${result.stdout}    boot_delay_ms                     u16     b

NUCLEO Reports Its Tables Over The Debug UART
    [Documentation]    The same listing read from a board rather than a hosted
    ...    image. The tables are built from real hardware there, so a table
    ...    that only registers under the simulator would be caught here and
    ...    nowhere else.
    [Tags]    param    tables    nucleo    physical
    ${result}=    Run Param Tables Smoke On Serial
    HIL Command Should Pass    ${result}    PARAM TABLES RESULT: PASS
