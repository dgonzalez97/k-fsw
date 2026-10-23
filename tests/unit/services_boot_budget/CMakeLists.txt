cmake_minimum_required(VERSION 3.20.0)
get_filename_component(KFSW_WORKSPACE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-services")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_services_boot_budget_tests LANGUAGES C)
target_sources(app PRIVATE src/main.c
    "${KFSW_WORKSPACE_ROOT}/kfsw-services/src/golden/golden.c")
target_link_libraries(app PRIVATE kfsw::services MCUBOOT_BOOTUTIL)
zephyr_link_libraries(-Wl,--wrap=nvs_write -Wl,--wrap=nvs_read -Wl,--wrap=crc32_ieee -Wl,--wrap=sys_reboot)

# Supply MCUboot decisions without a hardware bootloader in this fixture.
target_compile_definitions(kfsw_services PRIVATE CONFIG_KFSW_FWU_MCUBOOT=1 CONFIG_KFSW_GOLDEN=1)
