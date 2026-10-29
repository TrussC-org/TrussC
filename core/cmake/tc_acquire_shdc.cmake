# Acquire the pinned shader compiler without exposing a partial executable.
# Kept separate so script-mode tests exercise the production acquisition path.
include_guard(GLOBAL)

function(trussc_acquire_shdc _TC_SOKOL_SHDC _TC_SOKOL_SHDC_URL
        _TC_SOKOL_SHDC_EXPECTED_HASH _TC_SOKOL_SHDC_DIR _TC_SOKOL_SHDC_COMMIT)
    # A valid prepopulated cache needs only read access, including on read-only mounts.
    if(EXISTS "${_TC_SOKOL_SHDC}")
        file(SHA256 "${_TC_SOKOL_SHDC}" _TC_HASH)
        if(_TC_HASH STREQUAL _TC_SOKOL_SHDC_EXPECTED_HASH)
            return()
        endif()
    endif()
    get_filename_component(_TC_TOOLS_DIR "${_TC_SOKOL_SHDC}" DIRECTORY)
    file(MAKE_DIRECTORY "${_TC_TOOLS_DIR}")
    # CMake >= 3.20 supports function-scoped locks. Leave the lock file in place:
    # removing it could give concurrent waiters locks on different files.
    # Wait for a live installer to finish, even on a slow but progressing transfer.
    file(LOCK "${_TC_SOKOL_SHDC}.lock" GUARD FUNCTION)
    # Recheck under the lock: another configure may already have installed it.
    if(EXISTS "${_TC_SOKOL_SHDC}")
        file(SHA256 "${_TC_SOKOL_SHDC}" _TC_HASH)
        if(_TC_HASH STREQUAL _TC_SOKOL_SHDC_EXPECTED_HASH)
            return()
        endif()
    endif()

    string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef _TC_SUFFIX)
    set(_TC_SOKOL_SHDC_DOWNLOAD "${_TC_SOKOL_SHDC}.${_TC_SUFFIX}.download")
    message(STATUS "Downloading pinned sokol-shdc...")
    # Bound (transient failures): three attempts allow recovery without endless retries.
    foreach(_TC_ATTEMPT RANGE 1 3)
        # Bound (stalled peer): abort after 60 seconds with no data so retries can proceed.
        file(DOWNLOAD "${_TC_SOKOL_SHDC_URL}" "${_TC_SOKOL_SHDC_DOWNLOAD}"
            INACTIVITY_TIMEOUT 60
            SHOW_PROGRESS
            STATUS _TC_DOWNLOAD_STATUS)
        list(GET _TC_DOWNLOAD_STATUS 0 _TC_DOWNLOAD_ERROR)
        if(NOT _TC_DOWNLOAD_ERROR)
            break()
        endif()
        file(REMOVE "${_TC_SOKOL_SHDC_DOWNLOAD}")
        if(_TC_ATTEMPT LESS 3)
            message(STATUS "sokol-shdc download failed (${_TC_DOWNLOAD_STATUS}), retrying in 5s (attempt ${_TC_ATTEMPT}/3)...")
            execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 5)
        endif()
    endforeach()
    if(_TC_DOWNLOAD_ERROR)
        message(FATAL_ERROR "Failed to download sokol-shdc: ${_TC_DOWNLOAD_STATUS}")
    endif()
    # An explicit SHA256 check lets us clean up before FATAL_ERROR;
    # file(DOWNLOAD EXPECTED_HASH) can stop before that cleanup runs.
    file(SHA256 "${_TC_SOKOL_SHDC_DOWNLOAD}" _TC_SOKOL_SHDC_ACTUAL_HASH)
    if(NOT _TC_SOKOL_SHDC_ACTUAL_HASH STREQUAL _TC_SOKOL_SHDC_EXPECTED_HASH)
        file(REMOVE "${_TC_SOKOL_SHDC_DOWNLOAD}")
        message(FATAL_ERROR
            "sokol-shdc SHA-256 mismatch for ${_TC_SOKOL_SHDC_DIR} at commit ${_TC_SOKOL_SHDC_COMMIT}:\n"
            "  expected: ${_TC_SOKOL_SHDC_EXPECTED_HASH}\n"
            "  actual:   ${_TC_SOKOL_SHDC_ACTUAL_HASH}\n"
            "  URL: ${_TC_SOKOL_SHDC_URL}\n"
            "The failed download was removed.")
    endif()

    # Set permissions before publication, so readers always see a ready binary.
    if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        file(CHMOD "${_TC_SOKOL_SHDC_DOWNLOAD}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    endif()
    # file(RENAME RESULT) needs CMake 3.21. cmake -E rename provides the same
    # same-directory atomic replacement with a recoverable result on 3.20.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E rename
        "${_TC_SOKOL_SHDC_DOWNLOAD}" "${_TC_SOKOL_SHDC}"
        RESULT_VARIABLE _TC_RENAME_RESULT ERROR_VARIABLE _TC_RENAME_ERROR)
    file(REMOVE "${_TC_SOKOL_SHDC_DOWNLOAD}")
    if(NOT _TC_RENAME_RESULT STREQUAL "0")
        # A competing installer may have won, or Windows may have the old
        # executable open. Accept only the pinned binary; never remove it.
        if(EXISTS "${_TC_SOKOL_SHDC}")
            file(SHA256 "${_TC_SOKOL_SHDC}" _TC_HASH)
            if(_TC_HASH STREQUAL _TC_SOKOL_SHDC_EXPECTED_HASH)
                return()
            endif()
        endif()
        message(FATAL_ERROR "Failed to install sokol-shdc: ${_TC_RENAME_ERROR}")
    endif()
    message(STATUS "sokol-shdc downloaded and SHA-256 verified (${_TC_SOKOL_SHDC_COMMIT})")
endfunction()
