cmake_minimum_required(VERSION 3.20.0)

get_filename_component(KFSW_WORKSPACE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../.."
    ABSOLUTE
)

list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_platform_storage_zms_tests LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE kfsw::platform)

target_include_directories(app PRIVATE "${KFSW_WORKSPACE_ROOT}/kfsw-platform/src")

zephyr_link_libraries(-Wl,--wrap=flash_area_read)

set_source_files_properties(${ZEPHYR_BASE}/subsys/kvss/zms/zms.c
    TARGET_DIRECTORY zephyr
    PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/flash_read.h"
)
