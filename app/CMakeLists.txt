cmake_minimum_required(VERSION 3.20.0)

get_filename_component(KFSW_WORKSPACE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    ABSOLUTE
)

# Keep the west-managed module composition explicit for every build profile.
list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-services"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms/third_party/libcsp"
    "${KFSW_WORKSPACE_ROOT}/kfsw-comms"
)

# Apply K-FSW's initial libcsp feature policy before Zephyr loads the module.
# RDP, HMAC, and promiscuous capture are deliberately deferred from this issue.
set(CSP_REPRODUCIBLE_BUILDS ON CACHE BOOL "" FORCE)
set(CSP_USE_RDP OFF CACHE BOOL "" FORCE)
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

target_sources(app PRIVATE
    src/main.c
)

target_sources_ifdef(CONFIG_KFSW_DEBUG_SHELL app PRIVATE
    src/debug_shell.c
)

target_link_libraries(app PRIVATE
    kfsw::services
    kfsw::comms
)
