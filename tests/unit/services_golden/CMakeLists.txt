cmake_minimum_required(VERSION 3.20.0)
get_filename_component(KFSW_WORKSPACE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-services"
)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_services_golden_tests LANGUAGES C)
# Compile the actual installer without a hardware bootloader in this fixture.
target_sources(app PRIVATE src/main.c "${KFSW_WORKSPACE_ROOT}/kfsw-services/src/golden/golden.c")
# Enable the installer's public declarations without a simulated MCUboot boot flow.
set_source_files_properties("${KFSW_WORKSPACE_ROOT}/kfsw-services/src/golden/golden.c"
    PROPERTIES COMPILE_DEFINITIONS CONFIG_KFSW_GOLDEN=1)
target_link_libraries(app PRIVATE kfsw::services MCUBOOT_BOOTUTIL)
zephyr_link_libraries(-Wl,--wrap=flash_area_flatten)
