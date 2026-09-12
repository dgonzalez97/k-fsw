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
project(kfsw_services_ftp_client_tests LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE
    "${KFSW_WORKSPACE_ROOT}/kfsw-services/src/ftp"
)
target_link_libraries(app PRIVATE kfsw::services kfsw::comms)

# The client's side of the same seam. A fake link lets a test be the peer, and
# so ask the questions a real one will not answer on demand: a reply to a
# request that already timed out, a reply of the wrong kind, a peer that
# accepts the connection and then says nothing.
zephyr_link_libraries(
    -Wl,--wrap=kfsw_ftp_link_receive
    -Wl,--wrap=kfsw_ftp_link_release
    -Wl,--wrap=kfsw_ftp_link_send
    -Wl,--wrap=kfsw_ftp_link_max_payload
    -Wl,--wrap=kfsw_ftp_link_connect
    -Wl,--wrap=kfsw_ftp_link_close
    -Wl,--wrap=kfsw_ftp_link_is_ready
    -Wl,--wrap=kfsw_ftp_link_local_node
    -Wl,--wrap=kfsw_ftp_link_max_node
)
