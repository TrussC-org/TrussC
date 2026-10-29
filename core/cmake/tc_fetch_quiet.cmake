include_guard(GLOBAL)
# Callers include FetchContent when declaring dependencies. Early recovery must
# not initialize its cache before the app's environment/cache hooks.

function(_tc_restore_fetch_warning_cache)
    if(DEFINED CACHE{_TC_FETCH_WARN_SAVED})
        if(_TC_FETCH_WARN_SAVED)
            set(CMAKE_WARN_DEPRECATED "$CACHE{_TC_FETCH_WARN_VALUE}"
                CACHE "$CACHE{_TC_FETCH_WARN_TYPE}" "$CACHE{_TC_FETCH_WARN_HELP}" FORCE)
        else()
            unset(CMAKE_WARN_DEPRECATED CACHE)
        endif()
        unset(_TC_FETCH_WARN_SAVED CACHE)
        unset(_TC_FETCH_WARN_VALUE CACHE)
        unset(_TC_FETCH_WARN_TYPE CACHE)
        unset(_TC_FETCH_WARN_HELP CACHE)
    endif()
endfunction()

# A fatal dependency configure still saves the cache. Recover before other
# configure work, even if this run no longer requests that dependency.
_tc_restore_fetch_warning_cache()

function(tc_fetch_quiet name)
    _tc_restore_fetch_warning_cache()
    # Function entry saves the caller's normal binding (including absence or
    # a cache shadow); function exit restores it without changing caller scope.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
        # CMake 4.x compatibility diagnostics need a scoped policy floor.
        if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM OR
           CMAKE_POLICY_VERSION_MINIMUM VERSION_LESS "3.10")
            set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
        endif()
        FetchContent_MakeAvailable(${name})
    else()
        if(DEFINED CACHE{CMAKE_WARN_DEPRECATED})
            get_property(_tc_warn CACHE CMAKE_WARN_DEPRECATED PROPERTY VALUE)
            get_property(_tc_type CACHE CMAKE_WARN_DEPRECATED PROPERTY TYPE)
            get_property(_tc_help CACHE CMAKE_WARN_DEPRECATED PROPERTY HELPSTRING)
            set(_TC_FETCH_WARN_VALUE "${_tc_warn}" CACHE INTERNAL "" FORCE)
            set(_TC_FETCH_WARN_TYPE "${_tc_type}" CACHE INTERNAL "" FORCE)
            set(_TC_FETCH_WARN_HELP "${_tc_help}" CACHE INTERNAL "" FORCE)
            set(_TC_FETCH_WARN_SAVED TRUE CACHE INTERNAL "Restore dependency warning cache" FORCE)
        else()
            set(_TC_FETCH_WARN_SAVED FALSE CACHE INTERNAL "Restore dependency warning cache" FORCE)
        endif()
        # CMake 3.x compatibility diagnostics read the cache diagnostic setting.
        set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
        set(CMAKE_WARN_DEPRECATED OFF)
        FetchContent_MakeAvailable(${name})
        _tc_restore_fetch_warning_cache()
    endif()
endfunction()
