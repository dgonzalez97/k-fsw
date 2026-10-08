*** Settings ***
Library    OperatingSystem
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

Resolve KFSW Python
    [Documentation]    The workspace venv when there is one, python3 otherwise,
    ...                the way tools/ci/integration.sh already decides it. A
    ...                hard-coded venv path passes here and fails on a runner
    ...                that has none.
    ${from_env}=    Get Environment Variable    KFSW_PYTHON    ${EMPTY}
    IF    $from_env
        RETURN    ${from_env}
    END
    ${venv}=    Normalize Path    ${KFSW_REPO_DIR}/../.venv/bin/python
    ${exists}=    Run Keyword And Return Status    File Should Exist    ${venv}
    IF    $exists
        RETURN    ${venv}
    END
    RETURN    python3

Open KFSW Ground Console
    Reset KFSW Terminal Output
    ${python}=    Resolve KFSW Python
    TerminalRunner.Start Program
    ...    ${python}
    ...    params=${KFSW_REPO_DIR}/tests/k-ground-flight-terminal.py
    ...    wait_time=0
    Wait Until Keyword Succeeds    90s    250ms
    ...    KFSW Terminal Output Should Contain    K-GROUND FLIGHT TERMINAL: READY
