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
set(CSP_USE_RDP ON CACHE BOOL "" FORCE)
set(CSP_USE_HMAC OFF CACHE BOOL "" FORCE)
set(CSP_USE_PROMISC OFF CACHE BOOL "" FORCE)

list(APPEND ZEPHYR_EXTRA_MODULES "${KFSW_WORKSPACE_ROOT}/kfsw-modules")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_radio_crypto_tests LANGUAGES C)
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE "${KFSW_WORKSPACE_ROOT}/kfsw-modules/radio-uhf/src")
target_link_libraries(app PRIVATE kfsw::radio_uhf mbedTLS)
zephyr_link_libraries(
    -Wl,--wrap=kfsw_uart_codec_register
    -Wl,--wrap=kfsw_uart_codec_control
    -Wl,--wrap=radio_crypto_store_load
    -Wl,--wrap=radio_crypto_store_save
    -Wl,--wrap=kfsw_radio_host_random
)
