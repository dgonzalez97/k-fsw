*** Settings ***
Documentation     Limits release noop through KISS and CAN from the same board.
Library           Process

*** Test Cases ***
Limits Release On Both Links And Rearm
    [Tags]    physical    can    nucleo    limits    needs-can    needs-uart
    ${result}=    Run Process    ${CURDIR}/limits/released-on-two-links.sh    --board    nucleo_l496zg
    ...    stdout=${OUTPUT DIR}/limits-links.out    stderr=STDOUT    timeout=1800s
    Log    ${result.stdout}
    Skip If    "LIMITS LINKS RESULT: NOT RUN" in $result.stdout    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    LIMITS LINKS RESULT: PASS
