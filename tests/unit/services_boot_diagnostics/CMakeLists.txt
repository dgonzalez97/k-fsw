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
set(CSP_USE_RDP OFF CACHE BOOL "" FORCE)
set(CSP_USE_HMAC OFF CACHE BOOL "" FORCE)
set(CSP_USE_PROMISC OFF CACHE BOOL "" FORCE)

# Reuse the existing persistence fixture without defining another flash layout.
set(DTC_OVERLAY_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../services_param_persistence/boards/native_sim_native_64.overlay"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_services_boot_diagnostics_tests LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
)
target_include_directories(app PRIVATE
    ../../support/include
)
target_link_libraries(app PRIVATE kfsw::services)
