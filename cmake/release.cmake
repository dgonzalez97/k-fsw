option(KFSW_RELEASE_BUILD "Check and build a reproducible release image" OFF)
if(DEFINED ENV{KFSW_RELEASE_BUILD} AND NOT "$ENV{KFSW_RELEASE_BUILD}" STREQUAL "0")
    set(KFSW_RELEASE_BUILD ON CACHE BOOL "" FORCE)
endif()

if(KFSW_RELEASE_BUILD)
    execute_process(
        COMMAND "${KFSW_WORKSPACE_ROOT}/.venv/bin/python"
            "${KFSW_WORKSPACE_ROOT}/k-fsw/tools/release.py" check
            --output "${CMAKE_CURRENT_BINARY_DIR}/release-inputs.json"
        RESULT_VARIABLE kfsw_release_result
    )
    if(NOT kfsw_release_result EQUAL 0)
        message(FATAL_ERROR "Release input check failed")
    endif()
    set(CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION "$ENV{KFSW_IMAGE_VERSION}" CACHE STRING "" FORCE)
    set(CSP_REPRODUCIBLE_BUILDS ON CACHE BOOL "" FORCE)
else()
    set(CSP_REPRODUCIBLE_BUILDS OFF CACHE BOOL "" FORCE)
endif()
