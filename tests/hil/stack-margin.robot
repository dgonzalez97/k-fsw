*** Settings ***
Documentation     How much stack every thread on the NUCLEO has left, read from
...               the board. The host build measures pthread stacks and cannot
...               qualify these, so this is the only place the number means
...               anything, and it is published next to the coverage figure.
Resource          resources/common.resource
Library           Process

*** Variables ***
${STACK MARGIN}    ${CURDIR}/stm32/nucleo-l496zg/stack-margin.sh

*** Test Cases ***
Every Thread Keeps Room On Its Stack
    [Documentation]    Fails on a thin margin and on a stack that could not be
    ...                read, because a margin for part of the system is not a
    ...                margin for the system.
    [Tags]    physical    nucleo    resmon    stack
    ${result}=    Run Process    ${STACK MARGIN}
    ...           stdout=${TEMPDIR}/stack-margin.out    stderr=STDOUT    timeout=120s
    Log    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    STACK MARGIN RESULT:
    Should Not Contain    ${result.stdout}    RESULT: FAIL
    Should Contain    ${result.stdout}    0 unmeasured
