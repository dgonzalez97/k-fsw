*** Settings ***
Resource    common.resource
Library     OperatingSystem

*** Keywords ***
Require Hosted Ground Dependencies
    ${socat}=    Run Process    sh    -c    command -v socat    stderr=STDOUT
    Skip If    ${socat.rc} != 0    Install socat (sudo apt-get install socat) for PTY KISS.
    ${littlefs}=    Run Process    ${KFSW_PYTHON}    -c    import littlefs    stderr=STDOUT
    Skip If    ${littlefs.rc} != 0    Install littlefs-python in the Robot Python environment.

Run Hosted Fixture
    [Arguments]    ${name}    ${marker}    @{options}    ${csp_version}=${EMPTY}
    IF    $csp_version
        ${result}=    Run Process    ${KFSW_REPO_DIR}/tests/${name}    @{options}
        ...    env:KFSW_CSP_VERSION=${csp_version}
        ...    stdout=${OUTPUT DIR}/${TEST NAME}.out    stderr=STDOUT    timeout=900
    ELSE
        ${result}=    Run Process    ${KFSW_REPO_DIR}/tests/${name}    @{options}
        ...    stdout=${OUTPUT DIR}/${TEST NAME}.out    stderr=STDOUT    timeout=900
    END
    HIL Command Should Pass    ${result}    ${marker}
    RETURN    ${result.stdout}
