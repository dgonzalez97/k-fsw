*** Settings ***
Library    tmux_interaction_lib.TmuxInteractionLib    WITH NAME    TerminalRunner

*** Variables ***
${KFSW_REPO_DIR}    %{KFSW_REPO_DIR}
${KFSW_TERMINAL_OUTPUT}    ${EMPTY}

*** Keywords ***
Open KFSW Linux CSP Console
    Reset KFSW Terminal Output
    TerminalRunner.Start Program
    ...    ${KFSW_REPO_DIR}/tests/csp-smoke.sh
    ...    params=--terminal
    ...    wait_time=0
    Wait Until Keyword Succeeds
    ...    60s
    ...    250ms
    ...    KFSW Terminal Output Should Contain
    ...    KFSW CSP TERMINAL: READY

Open KFSW Linux Persistence Console
    Reset KFSW Terminal Output
    TerminalRunner.Start Program
    ...    ${KFSW_REPO_DIR}/tests/param-persistence-terminal.sh
    ...    wait_time=0
    Wait Until Keyword Succeeds
    ...    60s
    ...    250ms
    ...    KFSW Terminal Output Should Contain
    ...    KFSW PARAM TERMINAL: READY

Restart KFSW Persistence Console
    Reset KFSW Terminal Output
    TerminalRunner.Send Command    __KFSW_RESTART__
    Wait Until Keyword Succeeds
    ...    60s
    ...    250ms
    ...    KFSW Terminal Output Should Contain
    ...    KFSW PARAM TERMINAL: RESTARTED

Execute KFSW Command
    [Arguments]    ${command}    ${expected}
    Reset KFSW Terminal Output
    TerminalRunner.Send Command    ${command}
    Wait Until Keyword Succeeds
    ...    15s
    ...    250ms
    ...    KFSW Terminal Output Should Contain
    ...    ${expected}

KFSW Terminal Output Should Contain
    [Arguments]    ${expected}
    ${output}=    TerminalRunner.Read Output
    ${combined}=    Catenate    SEPARATOR=    ${KFSW_TERMINAL_OUTPUT}    ${output}
    Set Test Variable    $KFSW_TERMINAL_OUTPUT    ${combined}
    Log    ${KFSW_TERMINAL_OUTPUT}
    Should Contain    ${KFSW_TERMINAL_OUTPUT}    ${expected}

Reset KFSW Terminal Output
    Set Test Variable    $KFSW_TERMINAL_OUTPUT    ${EMPTY}

Close KFSW Terminal
    Run Keyword And Ignore Error    TerminalRunner.Close Program
    Sleep    500ms
