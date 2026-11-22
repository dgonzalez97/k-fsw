cmake_minimum_required(VERSION 3.20.0)

get_filename_component(KFSW_WORKSPACE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../.."
    ABSOLUTE
)

list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms/third_party/libcsp"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms"
)

set(CSP_REPRODUCIBLE_BUILDS ON CACHE BOOL "" FORCE)
set(CSP_USE_RDP OFF CACHE BOOL "" FORCE)
set(CSP_USE_HMAC OFF CACHE BOOL "" FORCE)
set(CSP_USE_PROMISC OFF CACHE BOOL "" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_comms_uart_validation_tests LANGUAGES C)

if(DEFINED UART_OPEN_FAILURE)
    target_sources(app PRIVATE src/partial_open.c)
    target_compile_definitions(app PRIVATE UART_OPEN_FAILURE=${UART_OPEN_FAILURE})
    zephyr_link_libraries(-Wl,--wrap=csp_usart_open)
    if(CONFIG_KFSW_CSP_UART_CODEC)
        zephyr_link_libraries(-Wl,--wrap=kfsw_uart_codec_attach
                             -Wl,--wrap=kfsw_uart_codec_check)
    endif()
else()
    target_sources(app PRIVATE src/main.c)
endif()
target_link_libraries(app PRIVATE kfsw::comms)
