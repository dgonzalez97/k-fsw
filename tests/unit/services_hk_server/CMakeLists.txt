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

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_services_hk_server_tests LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE kfsw::services kfsw::comms)

# Wrapping the connection is what lets a test ask what a request means without
# standing up a router: the fakes count the replies and can starve the pool.
zephyr_link_libraries(
    -Wl,--wrap=csp_send
    -Wl,--wrap=csp_buffer_get
    -Wl,--wrap=csp_buffer_free
)
