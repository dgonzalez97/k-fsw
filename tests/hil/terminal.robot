*** Settings ***
Documentation    Operator-style command execution through robot-terminal-runner.
Resource         resources/terminal.resource
Test Teardown    Close KFSW Terminal

*** Test Cases ***
KFSW Linux Can Command Remote CSP Node
    [Tags]    terminal    shell    csp
    Open KFSW Linux CSP Console
    Execute KFSW Command    kfsw status    K-FSW status
    Execute KFSW Command    kfsw version    K-FSW: kfsw-dev
    Execute KFSW Command    kfsw csp info    CSP node: 1
    Execute KFSW Command    kfsw csp routes    0/0 -> KISS direct
    Execute KFSW Command    kfsw csp ping 2    CSP ping 2: success
    Execute KFSW Command    kfsw param get 2 test_u32    2:test_u32 = 42
    Execute KFSW Command    kfsw param set 2 test_u32 1234    2:test_u32 = 1234
    Execute KFSW Command    kfsw param get 2 test_u32    2:test_u32 = 1234

KFSW Linux Rejects Invalid Remote Parameter Operations
    [Tags]    terminal    shell    csp    param
    Open KFSW Linux CSP Console
    Execute KFSW Command
    ...    kfsw param get 2 missing
    ...    get: parameter 'missing' not found
    Execute KFSW Command
    ...    kfsw param set 2 node_id 7
    ...    set: parameter 'node_id' is read-only

KFSW Linux Storage Is Ready And Writable
    [Tags]    terminal    shell    storage
    Open KFSW Linux CSP Console
    Execute KFSW Command    kfsw storage info    mount_point: /kfsw
    Execute KFSW Command    kfsw storage info    ready: yes
    Execute KFSW Command    kfsw storage test    Storage test: PASS

KFSW Linux Persists Parameters Across Restart
    [Tags]    terminal    shell    storage    param    persistence
    Open KFSW Linux Persistence Console
    Execute KFSW Command    kfsw param get test_u32    test_u32 = 42
    Execute KFSW Command    kfsw param set test_u32 1234    test_u32 = 1234
    Execute KFSW Command    kfsw param save    Parameter snapshot save: PASS
    Restart KFSW Persistence Console
    Execute KFSW Command    kfsw param get test_u32    test_u32 = 1234
    Execute KFSW Command    kfsw param defaults    saved snapshot unchanged
    Execute KFSW Command    kfsw param get test_u32    test_u32 = 42
    Execute KFSW Command    kfsw param load    Parameter snapshot load: PASS
    Execute KFSW Command    kfsw param get test_u32    test_u32 = 1234
    Execute KFSW Command    kfsw param clear    RAM unchanged
    Restart KFSW Persistence Console
    Execute KFSW Command    kfsw param get test_u32    test_u32 = 42
