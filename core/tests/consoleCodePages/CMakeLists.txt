cmake_minimum_required(VERSION 3.16)
project(consoleCodePages CXX)

# Compile the actual Windows console paths against a small Win32 mock. No
# TrussC link or display needed; keep the production code in its original files.
function(extract_region output source begin_marker end_marker)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" content)
    string(FIND "${content}" "${begin_marker}" begin_pos)
    if(begin_pos LESS 0)
        message(FATAL_ERROR "Missing start marker: ${begin_marker}")
    endif()
    string(SUBSTRING "${content}" ${begin_pos} -1 tail)
    string(FIND "${tail}" "${end_marker}" end_pos)
    if(end_pos LESS 0)
        message(FATAL_ERROR "Missing end marker: ${end_marker}")
    endif()
    string(SUBSTRING "${tail}" 0 ${end_pos} region)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${output}" "${region}")
endfunction()
set(headers "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
extract_region(sokol_state.inc "${headers}/sokol/sokol_app_tc.h"
    "        bool console_output_utf8_set;" "        /* clipboard */")
extract_region(sokol_console.inc "${headers}/sokol/sokol_app_tc.h"
    "static void _sapp_tc_win32_init_console(void) {" "/* The device belongs to the app")
extract_region(console_guard.inc "${headers}/TrussC.h"
    "struct ConsoleCodePages {" "} // namespace internal")
extract_region(crash_state.inc "${headers}/tc/app/tcCrashHandler.cpp"
    "std::atomic<uint64_t> crashConsoleCodePages" "using AbortHandler")
extract_region(crash_setter.inc "${headers}/tc/app/tcCrashHandler.cpp"
    "void setCrashConsoleCodePages(" "#endif")
extract_region(crash_exception.inc "${headers}/tc/app/tcCrashHandler.cpp"
    "LONG WINAPI onException(" "void onAbort(")

add_executable(consoleCodePages main.cpp)
target_compile_features(consoleCodePages PRIVATE cxx_std_17)
target_include_directories(consoleCodePages PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(MSVC)
    target_compile_options(consoleCodePages PRIVATE /W4)
else()
    target_compile_options(consoleCodePages PRIVATE -Wall -Wextra -Werror)
endif()
