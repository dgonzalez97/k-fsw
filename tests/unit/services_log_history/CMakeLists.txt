cmake_minimum_required(VERSION 3.20.0)
get_filename_component(KFSW_WORKSPACE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms/third_party/libcsp"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms"
    "${KFSW_WORKSPACE_ROOT}/kfsw-services"
)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_log_history_tests LANGUAGES C)
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${KFSW_WORKSPACE_ROOT}/kfsw-services/src)
target_link_libraries(app PRIVATE kfsw::services kfsw::comms)
zephyr_link_libraries(-Wl,--wrap=kfsw_log_history_get_encoded)
