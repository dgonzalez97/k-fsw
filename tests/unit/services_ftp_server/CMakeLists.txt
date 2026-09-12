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
project(kfsw_services_ftp_server_tests LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE
    "${KFSW_WORKSPACE_ROOT}/kfsw-services/src/ftp"
)
target_link_libraries(app PRIVATE kfsw::services kfsw::comms)

# The link is the seam the server was already built around. Standing a fake one
# up in its place is what lets a request be asked and its answer read without a
# radio, a router or a second node.
zephyr_link_libraries(
    -Wl,--wrap=kfsw_ftp_link_receive
    -Wl,--wrap=kfsw_ftp_link_release
    -Wl,--wrap=kfsw_ftp_link_send
    -Wl,--wrap=kfsw_ftp_link_max_payload
)
