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
