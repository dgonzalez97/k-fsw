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
project(kfsw_app_hk_entries_tests LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    "${KFSW_WORKSPACE_ROOT}/k-fsw/app/src/hk_entries.c"
)
target_include_directories(app PRIVATE "${KFSW_WORKSPACE_ROOT}/k-fsw/app/src")
target_link_libraries(app PRIVATE kfsw::services)
