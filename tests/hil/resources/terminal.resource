*** Settings ***
Library    tmux_interaction_lib.TmuxInteractionLib    WITH NAME    TerminalRunner

*** Variables ***
${KFSW_REPO_DIR}    %{KFSW_REPO_DIR}

*** Keywords ***
Open KFSW Linux CSP Console
    TerminalRunner.Start Program
    ...    ${KFSW_REPO_DIR}/tests/csp-smoke.sh
    ...    params=--terminal
    ...    wait_time=0
    Wait Until Keyword Succeeds
    ...    60s
    ...    250ms
    ...    KFSW Terminal Output Should Contain
    ...    KFSW CSP TERMINAL: READY

Execute KFSW Command
    [Arguments]    ${command}    ${expected}
    TerminalRunner.Send Command    ${command}
    Wait Until Keyword Succeeds
    ...    15s
    ...    250ms
    ...    KFSW Terminal Output Should Contain
    ...    ${expected}

KFSW Terminal Output Should Contain
    [Arguments]    ${expected}
    ${output}=    TerminalRunner.Read Output
    Log    ${output}
    Should Contain    ${output}    ${expected}

Close KFSW Terminal
    Run Keyword And Ignore Error    TerminalRunner.Close Program
    Sleep    500ms
