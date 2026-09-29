# =============================================================================
# tc_hot_reload_scan.cmake — the ONE rule deciding whether a project uses
# TC_HOT_RELOAD. Included both by trussc_app.cmake at configure time and by the
# generated pre-build check (_tc_check_hot_reload.cmake, run with cmake -P) on
# every build. The two must agree: when they didn't (#234 — a recursive scan
# at configure, a top-level-only scan and a different comment rule at build),
# a project could never build.
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
