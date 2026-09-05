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
#
# Reproducible builds are off, deliberately. With them on libcsp blanks the
# date and time in its identity reply, and that reply is how ground tells one
# image from another: an identity that cannot say when it was built defeats the
# question it exists to answer. ccache will not reuse an object compiled from a
# file containing __DATE__ unless told to be sloppy, so the date stays true.
set(CSP_REPRODUCIBLE_BUILDS OFF CACHE BOOL "" FORCE)
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

# Core parameter tables, identifiers 1 to 24. They live in the composition
# layer because kfsw-platform and kfsw-comms sit below the parameter service.
target_sources_ifdef(CONFIG_KFSW_DEBUG_SHELL app PRIVATE
    src/shell/shell_prompt.c
)

# Runs before Zephyr's default handler so a crash leaves its address behind,
# then hands over unchanged.
target_sources_ifdef(CONFIG_KFSW_LASTWORDS app PRIVATE
    src/fatal.c
)

if(CONFIG_KFSW_DEBUG_SHELL AND CONFIG_KFSW_COMMAND)
    target_sources(app PRIVATE src/shell/shell_echo.c)
endif()

target_sources_ifdef(CONFIG_KFSW_PARAM app PRIVATE
    src/parameters/board_table.c
    src/parameters/system_table.c
    src/parameters/telemetry_table.c
)

if(CONFIG_KFSW_PARAM AND CONFIG_KFSW_CSP)
    target_sources(app PRIVATE src/parameters/csp_table.c)
endif()

if(CONFIG_KFSW_PARAM AND CONFIG_KFSW_STORAGE)
    target_sources(app PRIVATE src/parameters/storage_table.c)
endif()

if(CONFIG_KFSW_PARAM AND CONFIG_KFSW_WATCHDOG)
    target_sources(app PRIVATE src/parameters/watchdog_table.c)
endif()

target_sources_ifdef(CONFIG_KFSW_COMMAND app PRIVATE
    src/commands/command_definitions.c
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
    target_sources_ifdef(CONFIG_KFSW_COMMAND app PRIVATE
        src/shell/shell_command.c
    )
    target_sources_ifdef(CONFIG_KFSW_EVENT app PRIVATE
        src/shell/shell_event.c
    )
    target_sources_ifdef(CONFIG_KFSW_WATCHDOG app PRIVATE
        src/shell/shell_watchdog.c
    )
    target_sources_ifdef(CONFIG_KFSW_FWU app PRIVATE
        src/shell/shell_fwu.c
    )
    target_sources_ifdef(CONFIG_KFSW_HEALTH app PRIVATE
        src/shell/shell_health.c
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
