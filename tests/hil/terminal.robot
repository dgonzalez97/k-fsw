*** Settings ***
Documentation    Operator-style command execution through robot-terminal-runner.
Resource         resources/terminal.resource
Test Teardown    Close KFSW Terminal

*** Test Cases ***
KFSW Linux Can Command Remote CSP Node
    [Tags]    terminal    shell    csp
    Open KFSW Linux CSP Console
    Execute KFSW Command    status    K-FSW status
    Execute KFSW Command    version    K-FSW: kfsw-dev
    Execute KFSW Command    csp info    CSP node: 1
    Execute KFSW Command    csp routes    0/0 -> KISS direct
    Execute KFSW Command    csp ping 2    CSP ping 2: success
    Execute KFSW Command    param get 2 test_u32    2:test_u32 = 42
    Execute KFSW Command    param set 2 test_u32 1234    2:test_u32 = 1234
    Execute KFSW Command    param get 2 test_u32    2:test_u32 = 1234

KFSW Linux Rejects Invalid Remote Parameter Operations
    [Tags]    terminal    shell    csp    param
    Open KFSW Linux CSP Console
    Execute KFSW Command
    ...    param get 2 missing
    ...    get: parameter 'missing' not found
    Execute KFSW Command
    ...    param set 2 node_id 7
    ...    set: parameter 'node_id' is read-only

KFSW Linux Storage Is Ready And Writable
    [Tags]    terminal    shell    storage
    Open KFSW Linux CSP Console
    Execute KFSW Command    storage info    mount_point: /kfsw
    Execute KFSW Command    storage info    ready: yes
    Execute KFSW Command    storage test    Storage test: PASS

KFSW Linux Transfers Files Through Remote CSP Node
    [Tags]    terminal    shell    csp    ftp    storage
    Open KFSW Linux CSP Console
    Execute KFSW Command
    ...    ftp generate /build/robot.bin 1024
    ...    FTP generate path=/build/robot.bin: PASS bytes=1024
    Execute KFSW Command    ftp 2 mkdir /robot    FTP mkdir node=2 path=/robot: PASS
    Execute KFSW Command
    ...    ftp put 2 /build/robot.bin /robot/upload.bin
    ...    FTP put node=2 source=/build/robot.bin destination=/robot/upload.bin: PASS bytes=1024
    Execute KFSW Command
    ...    ftp stat 2 /robot/upload.bin
    ...    FTP stat node=2 path=/robot/upload.bin type=file bytes=1024
    Execute KFSW Command    ftp 2 ls /robot    FTP list: PASS entries=1
    Execute KFSW Command
    ...    ftp get 2 /robot/upload.bin /build/robot-returned.bin
    ...    FTP get node=2 source=/robot/upload.bin destination=/build/robot-returned.bin: PASS bytes=1024
    Execute KFSW Command
    ...    ftp verify /build/robot.bin /build/robot-returned.bin
    ...    FTP verify first=/build/robot.bin second=/build/robot-returned.bin: PASS
    Execute KFSW Command
    ...    ftp get 2 /robot/missing.bin /build/missing.bin
    ...    FTP get node=2 path=/robot/missing.bin: not found
    Execute KFSW Command
    ...    ftp stat 2 ../params/parameters.dat
    ...    invalid path/request
    Execute KFSW Command    csp ping 2    CSP ping 2: success
    Execute KFSW Command    param get 2 test_u32    2:test_u32 = 42

KFSW Linux Persists Parameters Across Restart
    [Tags]    terminal    shell    storage    param    persistence
    Open KFSW Linux Persistence Console
    Execute KFSW Command    param get test_u32    test_u32 = 42
    Execute KFSW Command    param set test_u32 1234    test_u32 = 1234
    Execute KFSW Command    param save    Parameter snapshot save: PASS
    Restart KFSW Persistence Console
    Execute KFSW Command    param get test_u32    test_u32 = 1234
    Execute KFSW Command    param defaults    saved snapshot unchanged
    Execute KFSW Command    param get test_u32    test_u32 = 42
    Execute KFSW Command    param load    Parameter snapshot load: PASS
    Execute KFSW Command    param get test_u32    test_u32 = 1234
    Execute KFSW Command    param clear    RAM unchanged
    Restart KFSW Persistence Console
    Execute KFSW Command    param get test_u32    test_u32 = 42
