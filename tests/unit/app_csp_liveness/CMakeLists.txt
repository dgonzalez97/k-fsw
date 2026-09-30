cmake_minimum_required(VERSION 3.20.0)

get_filename_component(KFSW_WORKSPACE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../.."
    ABSOLUTE
)

list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-services"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_app_csp_liveness_tests LANGUAGES C)

# The probe is what is under test, so the router it calls is replaced here
# rather than brought up: no interfaces, no buffers, and a return value the
# test chooses.
target_sources(app PRIVATE
    src/main.c
    "${KFSW_WORKSPACE_ROOT}/k-fsw/app/src/csp_liveness.c"
)
target_include_directories(app PRIVATE
    "${KFSW_WORKSPACE_ROOT}/k-fsw/app/src"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms/include"
)
target_link_libraries(app PRIVATE kfsw::services)
