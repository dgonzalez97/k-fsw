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
project(kfsw_services_hk_beacon_tests LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE kfsw::services kfsw::comms)

# The link is the seam. A beacon's whole contract is what it puts on the wire
# and when it refuses to, and neither can be observed through a running router:
# wrapping the three calls lets a test read the addressing off the packet and
# starve the pool on demand.
zephyr_link_libraries(
    -Wl,--wrap=csp_sendto
    -Wl,--wrap=csp_buffer_get
    -Wl,--wrap=csp_buffer_remaining
)
