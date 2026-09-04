cmake_minimum_required(VERSION 3.20.0)

get_filename_component(KFSW_WORKSPACE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../.."
    ABSOLUTE
)

list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms/third_party/libcsp"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms"
    "${KFSW_WORKSPACE_ROOT}/kfsw-services"
)

set(CSP_REPRODUCIBLE_BUILDS ON CACHE BOOL "" FORCE)
set(CSP_USE_RDP OFF CACHE BOOL "" FORCE)
set(CSP_USE_HMAC OFF CACHE BOOL "" FORCE)
set(CSP_USE_PROMISC OFF CACHE BOOL "" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_app_param_tables_tests LANGUAGES C)

# The tables under test are the ones the application composes, not a copy of
# them: a test against its own definitions would prove only that the test is
# self-consistent.
target_sources(app PRIVATE
    src/main.c
    ../../../app/src/parameters/board_table.c
    ../../../app/src/parameters/system_table.c
    ../../../app/src/parameters/telemetry_table.c
    ../../../app/src/parameters/csp_table.c
    ../../../app/src/parameters/storage_table.c
    ../../../app/src/parameters/watchdog_table.c
)
target_include_directories(app PRIVATE
    ../../../app/src
)
target_link_libraries(app PRIVATE kfsw::services kfsw::comms)
