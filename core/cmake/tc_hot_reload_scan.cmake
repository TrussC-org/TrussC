# =============================================================================
# tc_hot_reload_scan.cmake — the ONE rule deciding whether a project uses
# TC_HOT_RELOAD. Included both by trussc_app.cmake at configure time and by the
# generated pre-build check (_tc_check_hot_reload.cmake, run with cmake -P) on
# every build. The two must agree: when they didn't (#234 — a recursive scan
# at configure, a top-level-only scan and a different comment rule at build),
# a project could never build. The same holds for the platform gate (#329):
# both callers go through tc_hot_reload_decide() so a TC_HOT_RELOAD in source
# is ignored the same way on platforms without hot reload.
#
# tc_hot_reload_decide(<out-var> <platform-supported> <file>...)
#   The full decision. Sets <out-var> to OFF without scanning when
#   <platform-supported> is false (Emscripten / Android / iOS: the macro is a
#   no-op there and the app builds normally), else to tc_hot_reload_scan()'s
#   result for the given files. <platform-supported> is computed once at
#   configure time by trussc_app.cmake and baked into the pre-build check,
#   because EMSCRIPTEN / ANDROID are undefined in cmake -P mode.
#
# tc_hot_reload_scan(<out-var> <file>...)
#   Sets <out-var> to ON when any of the given .cpp files contains
#   TC_HOT_RELOAD with no '/' before it on its line (so `// TC_HOT_RELOAD`,
#   `foo(); // TC_HOT_RELOAD` and `/* TC_HOT_RELOAD */` don't count), else OFF.
#   Non-.cpp and missing files are skipped.
# =============================================================================
function(tc_hot_reload_scan OUT_VAR)
    set(_found OFF)
    foreach(_src IN LISTS ARGN)
        if(_src MATCHES "\\.cpp$" AND EXISTS "${_src}")
            file(READ "${_src}" _content)
            if(_content MATCHES "(^|\n)[^/\n]*TC_HOT_RELOAD")
                set(_found ON)
                break()
            endif()
        endif()
    endforeach()
    set(${OUT_VAR} ${_found} PARENT_SCOPE)
endfunction()

function(tc_hot_reload_decide OUT_VAR PLATFORM_SUPPORTED)
    if(PLATFORM_SUPPORTED)
        tc_hot_reload_scan(_result ${ARGN})
    else()
        set(_result OFF)
    endif()
    set(${OUT_VAR} ${_result} PARENT_SCOPE)
endfunction()
