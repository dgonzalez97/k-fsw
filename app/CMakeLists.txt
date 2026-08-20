cmake_minimum_required(VERSION 3.20.0)

get_filename_component(KFSW_WORKSPACE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    ABSOLUTE
)

# Use local modules until published revisions are added to west.yml.
list(APPEND ZEPHYR_EXTRA_MODULES
    "${KFSW_WORKSPACE_ROOT}/kfsw-platform"
    "${KFSW_WORKSPACE_ROOT}/kfsw-services"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(kfsw LANGUAGES C)

if(NOT TARGET kfsw_services)
    message(FATAL_ERROR
        "kfsw-services was not discovered as a Zephyr module")
endif()

target_sources(app PRIVATE
    src/main.c
)

target_sources_ifdef(CONFIG_KFSW_DEBUG_SHELL app PRIVATE
    src/debug_shell.c
)

target_link_libraries(app PRIVATE
    kfsw::services
)
