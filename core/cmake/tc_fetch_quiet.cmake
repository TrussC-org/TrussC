include_guard(GLOBAL)
include(FetchContent)

function(tc_fetch_quiet name)
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
            set(_tc_had_cache TRUE)
            get_property(_tc_warn CACHE CMAKE_WARN_DEPRECATED PROPERTY VALUE)
            get_property(_tc_type CACHE CMAKE_WARN_DEPRECATED PROPERTY TYPE)
            get_property(_tc_help CACHE CMAKE_WARN_DEPRECATED PROPERTY HELPSTRING)
        else()
            set(_tc_had_cache FALSE)
        endif()
        # CMake 3.x compatibility diagnostics read the cache diagnostic setting.
        set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
        set(CMAKE_WARN_DEPRECATED OFF)
        FetchContent_MakeAvailable(${name})
        if(_tc_had_cache)
            set(CMAKE_WARN_DEPRECATED "${_tc_warn}" CACHE ${_tc_type} "${_tc_help}" FORCE)
        else()
            unset(CMAKE_WARN_DEPRECATED CACHE)
        endif()
    endif()
endfunction()
