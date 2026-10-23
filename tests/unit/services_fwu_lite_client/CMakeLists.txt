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

set(CSP_USE_RDP ON CACHE BOOL "" FORCE)
set(CSP_REPRODUCIBLE_BUILDS ON CACHE BOOL "" FORCE)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_services_fwu_lite_client_tests LANGUAGES C)

target_sources(app PRIVATE src/main.c)
# Inspect the pinned RDP read timeout with a real static connection in this fixture.
target_include_directories(app PRIVATE
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms/third_party/libcsp/src")
target_link_libraries(app PRIVATE kfsw::services kfsw::comms)

zephyr_link_libraries(
    -Wl,--wrap=csp_connect -Wl,--wrap=csp_close
    -Wl,--wrap=csp_send -Wl,--wrap=csp_read
    -Wl,--wrap=csp_buffer_get -Wl,--wrap=csp_buffer_free
    -Wl,--wrap=nsi_host_open -Wl,--wrap=nsi_host_read -Wl,--wrap=nsi_host_close
    -Wl,--wrap=stream_flash_buffered_write
)
