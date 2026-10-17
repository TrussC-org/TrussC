# Shared Makefile build ownership (#885). Used by configure and the build check.
function(tc_shared_build_read_owner stamp result)
    set(owner "")
    if(EXISTS "${stamp}")
        # Bound (platform): an owner is one build path, at most 4096 bytes. Overflow is treated as another owner and triggers one reconfigure.
        # CMake 3.28 can allocate for the whole file even with READ LIMIT.
        file(SIZE "${stamp}" size)
        if(size LESS_EQUAL 4096)
            file(READ "${stamp}" owner LIMIT 4097)
            string(LENGTH "${owner}" size)
            if(size GREATER 4096)
                set(owner "")
            endif()
        endif()
    endif()
    set(${result} "${owner}" PARENT_SCOPE)
endfunction()

function(tc_shared_build_guard_targets directory)
    # Guard every target in this app's tree, including core shader generation,
    # addons and hot-reload guests, before entering any shared-folder rule.
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        if(NOT target STREQUAL "_tc_shared_build_check")
            add_dependencies(${target} _tc_shared_build_check)
        endif()
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        tc_shared_build_guard_targets("${subdirectory}")
    endforeach()
endfunction()
