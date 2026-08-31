cmake_minimum_required(VERSION 3.20.0)

get_filename_component(KFSW_WORKSPACE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    ABSOLUTE
)

# Keep the west-managed module composition explicit for every build.
list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms/third_party/libcsp"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms"
    "${KFSW_WORKSPACE_ROOT}/kfsw-services"
    "${KFSW_WORKSPACE_ROOT}/kfsw-modules"
)

# Apply K-FSW's libcsp feature policy before Zephyr loads the module. The FTP
# service uses RDP for bounded flow control and retransmission.
set(CSP_REPRODUCIBLE_BUILDS ON CACHE BOOL "" FORCE)
set(CSP_USE_RDP ON CACHE BOOL "" FORCE)
set(CSP_USE_HMAC OFF CACHE BOOL "" FORCE)
set(CSP_USE_PROMISC OFF CACHE BOOL "" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(kfsw LANGUAGES C)

if(NOT TARGET kfsw_services)
    message(FATAL_ERROR
        "kfsw-services was not discovered as a Zephyr module")
endif()

if(NOT TARGET kfsw_comms)
    message(FATAL_ERROR
        "kfsw-comms was not discovered as a Zephyr module")
endif()

if(NOT ZEPHYR_KFSW_MODULES_MODULE_DIR)
    message(FATAL_ERROR
        "kfsw-modules was not discovered as a Zephyr module")
endif()

target_sources(app PRIVATE
    src/main.c
)

target_sources_ifdef(CONFIG_KFSW_PARAM app PRIVATE
    src/parameter_definitions.c
)

if(CONFIG_KFSW_PARAM_TEST_DEFINITIONS)
    target_sources(app PRIVATE
        ../tests/support/parameter_definitions.c
    )
    target_include_directories(app PRIVATE
        ../tests/support/include
    )
endif()

if(CONFIG_KFSW_DEBUG_SHELL)
    target_sources(app PRIVATE
        src/shell/shell_system.c
    )

    target_sources_ifdef(CONFIG_KFSW_CSP app PRIVATE
        src/shell/shell_csp.c
    )
    target_sources_ifdef(CONFIG_KFSW_CSP_KISS_UART app PRIVATE
        src/shell/shell_uart.c
    )
    target_sources_ifdef(CONFIG_KFSW_PARAM app PRIVATE
        src/shell/shell_param.c
    )
    target_sources_ifdef(CONFIG_KFSW_STORAGE app PRIVATE
        src/diagnostics/storage_diagnostics.c
        src/shell/shell_storage.c
    )
    target_sources_ifdef(CONFIG_KFSW_FTP app PRIVATE
        src/diagnostics/ftp_diagnostics.c
        src/shell/shell_ftp.c
    )
endif()

target_include_directories(app PRIVATE src)

target_link_libraries(app PRIVATE
    kfsw::platform
    kfsw::services
    kfsw::comms
)

if(CONFIG_KFSW_RADIO_UHF)
    if(NOT TARGET kfsw_radio_uhf)
        message(FATAL_ERROR
            "radio-uhf was enabled but kfsw-modules did not provide its target")
    endif()
    target_link_libraries(app PRIVATE kfsw::radio_uhf)
endif()

if(CONFIG_KFSW_BOTON_TEST)
    if(NOT TARGET kfsw_boton_test)
        message(FATAL_ERROR
            "boton_test was enabled but kfsw-modules did not provide its target")
    endif()
    target_link_libraries(app PRIVATE kfsw::boton_test)
endif()
