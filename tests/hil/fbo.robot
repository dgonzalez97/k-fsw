*** Settings ***
Documentation     Procedures, the time-tagged queue and remote execution on a
...               NUCLEO-L496ZG over physical CAN. The software smoke runs
...               procedures where the file is already on disk and the link is
...               a pipe; this one sends the procedure over the bus and lets
...               the queue release a command after the ground node is gone.
Resource          resources/common.resource
Library           Process

*** Variables ***
${FBO PASS PLAN}    ${CURDIR}/fbo/pass-plan.sh

*** Test Cases ***
A Procedure Crosses The Bus And The Queue Releases Without The Ground
    [Tags]    physical    can    nucleo    fbo    remexec    needs-can
    ${result}=    Run Process    ${FBO PASS PLAN}    --board    nucleo_l496zg
    ...           stdout=${OUTPUT DIR}/fbo-can.out    stderr=STDOUT    timeout=2400s
    Log    ${result.stdout}
    Skip If    "FBO CAN RESULT: NOT RUN" in $result.stdout    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    FBO CAN RESULT: PASS
    Should Contain    ${result.stdout}    released_with_nobody_listening=yes
