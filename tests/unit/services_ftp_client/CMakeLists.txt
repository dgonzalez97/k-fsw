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
target_include_directories(app PRIVATE "${KFSW_WORKSPACE_ROOT}/kfsw-services/src/ftp")
target_link_libraries(app PRIVATE kfsw::services kfsw::comms)

# The whole transport, so the peer is scripted rather than run. is_ready and the
# node limits are wrapped too: without them the client refuses before it reaches
# the exchange under test.
zephyr_link_libraries(
    -Wl,--wrap=kfsw_ftp_link_connect
    -Wl,--wrap=kfsw_ftp_link_send
    -Wl,--wrap=kfsw_ftp_link_receive
    -Wl,--wrap=kfsw_ftp_link_release
    -Wl,--wrap=kfsw_ftp_link_close
    -Wl,--wrap=kfsw_ftp_link_is_ready
    -Wl,--wrap=kfsw_ftp_link_local_node
    -Wl,--wrap=kfsw_ftp_link_max_node
)
