# Guest-only directory scope: never change the host/core configuration flags.
# Unoptimized code halves reload latency (232775ac); appending /Od to /O2
# produces MSVC D9025, even though GCC/Clang silently accept -O2 -O0.
get_cmake_property(_tc_variables VARIABLES)
foreach(_tc_variable IN LISTS _tc_variables)
    if(_tc_variable MATCHES "^CMAKE_(C|CXX|OBJC|OBJCXX)_FLAGS($|_)")
        # Include base flags and custom configurations, not just Release.
        # Match whole options so definitions and paths containing O2 survive.
        if(MSVC)
            set(_tc_optimization "[-/]O[12xd]")
        else()
            set(_tc_optimization "-O([0-9]+|g|s|z|fast)?")
        endif()
        while("${${_tc_variable}}" MATCHES "(^|[ \t])${_tc_optimization}([ \t]|$)")
            string(REGEX REPLACE "(^|[ \t])${_tc_optimization}([ \t]|$)" "\\1 "
                ${_tc_variable} "${${_tc_variable}}")
        endwhile()
        if(MSVC)
            # Embed debug info: a debugger can lock the guest's compiler PDB.
            # Replace /Zi and /ZI here too, without changing host debug info.
            string(REGEX REPLACE "/Z[iI]" "/Z7" ${_tc_variable} "${${_tc_variable}}")
        endif()
    endif()
endforeach()

# Explicit trussc_app(SOURCES ...) paths may be relative to the app directory.
get_directory_property(_tc_app_source_dir PARENT_DIRECTORY)
set(_tc_guest_sources "")
foreach(_tc_source IN LISTS _TC_GUEST_SOURCES)
    get_filename_component(_tc_source "${_tc_source}" ABSOLUTE
        BASE_DIR "${_tc_app_source_dir}")
    list(APPEND _tc_guest_sources "${_tc_source}")
endforeach()
add_library(guest SHARED ${_tc_guest_sources})
if(MSVC)
    target_compile_options(guest PRIVATE /Od /Z7)
    set_target_properties(guest PROPERTIES LINK_FLAGS "/DEBUG /PDBALTPATH:none")
else()
    target_compile_options(guest PRIVATE -O0)
endif()
