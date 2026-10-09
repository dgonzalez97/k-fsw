*** Settings ***
Documentation    Remote shell execution between two hosted native_sim nodes.
...              The ground node reads the flight node's allowlist, runs a
...              command the flight node offers, and is refused one it does
...              not. The transport is a socat-bridged pseudo-terminal pair
...              carrying KISS, so these cases cover the service, the wire
...              format and the allowlist over KISS framing. They say nothing
...              about a physical UART, a radio link or flight hardware; there
...              is no wiring and no RF in this suite.
Resource         resources/ground.resource

*** Test Cases ***
The Allowlist Is Both The Listing And The Permission
    [Documentation]    Ground asks node 1 what it offers, runs an offered
    ...    command and asserts its output, then is refused a command that is
    ...    registered with the flight shell but not marked. PTY KISS only.
    [Tags]    software    remexec    ground
    Require Hosted Ground Dependencies
    Open Ground Pair    ${OUTPUT DIR}/ground-remexec
    TRY
        ${listing}=    Ground Command    remexec 1 get    node: 1
        Should Contain    ${listing}    storage info
        Should Contain    ${listing}    time
        # Registered with the flight shell, absent from the allowlist, so
        # discovery must not report it either.
        Should Not Contain    ${listing}    storage test

        ${narrowed}=    Ground Command    remexec 1 get storage    node: 1
        Should Contain    ${narrowed}    Filesystem totals and mount state
        Should Not Contain    ${narrowed}    storage test

        ${ran}=    Ground Command    remexec 1 run time    monotonic_ms:
        Should Contain    ${ran}    status: ok

        ${refused}=    Ground Command    remexec 1 run storage test
        ...    'storage test' is not offered for remote execution
        # The command exists on the flight node, so proving it did not run is
        # the point: its own output never appears.
        Should Not Contain    ${refused}    Storage test: PASS

        # The serving node counted the refusal and says why, so an operator
        # sees it without reading a log.
        Ground Command    param get 1 remexec_refused    1:remexec_refused = 1
        Ground Command    param get 1 remexec_accepted    1:remexec_accepted = 1
    FINALLY
        Close Ground Pair
    END
