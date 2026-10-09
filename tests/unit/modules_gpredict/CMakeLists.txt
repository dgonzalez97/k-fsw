cmake_minimum_required(VERSION 3.20.0)

get_filename_component(KFSW_WORKSPACE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../.."
    ABSOLUTE
)

list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-services"
    "${KFSW_WORKSPACE_ROOT}/kfsw-modules"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_modules_gpredict_tests LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE
    "${KFSW_WORKSPACE_ROOT}/kfsw-modules/gpredict/src"
)
target_link_libraries(app PRIVATE kfsw::gpredict kfsw::services)

zephyr_link_libraries(-Wl,--wrap=kfsw_time_monotonic_ms)
