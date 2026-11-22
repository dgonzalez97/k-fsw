cmake_minimum_required(VERSION 3.20.0)
get_filename_component(KFSW_WORKSPACE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-services")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_health_fault_tests LANGUAGES C)
target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE kfsw::services)
zephyr_link_libraries(
    -Wl,--wrap=kfsw_platform_watchdog_release
    -Wl,--wrap=kfsw_platform_watchdog_feed
    -Wl,--wrap=kfsw_time_monotonic_ms
    -Wl,--wrap=kfsw_log_write)
