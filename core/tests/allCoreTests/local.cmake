# core/tests/allCoreTests: compile every combinable core test into this app.
#
# A test is combined when core/tests/<name>/src/main.cpp exists and the dir
# has no `own-binary` marker (examples/build_all.py uses the same rule).
# Every .cpp/.c under its src/ is added, and each gets
# TC_CORE_TEST_NAME="<name>", which turns its TC_CORE_TEST_MAIN into a
# registered entry (core/tests/common/tcCoreTest.h). A test's own quoted
# includes resolve relative to its source file, as when it is built alone.
#
# A test's own local.cmake is NOT included here (it is written for its own
# target and dir); what a combined test needs from it is repeated below.

get_filename_component(_TC_CORE_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
file(GLOB _TC_CORE_TEST_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS
    "${_TC_CORE_TESTS_DIR}/*")
list(SORT _TC_CORE_TEST_DIRS)

set(_TC_CORE_TEST_NAMES "")
foreach(_TC_DIR IN LISTS _TC_CORE_TEST_DIRS)
    get_filename_component(_TC_NAME "${_TC_DIR}" NAME)
    if(_TC_NAME STREQUAL "allCoreTests"
            OR NOT EXISTS "${_TC_DIR}/src/main.cpp"
            OR EXISTS "${_TC_DIR}/own-binary")
        continue()
    endif()
    file(GLOB_RECURSE _TC_TEST_SOURCES CONFIGURE_DEPENDS
        "${_TC_DIR}/src/*.cpp" "${_TC_DIR}/src/*.c")
    target_sources(${PROJECT_NAME} PRIVATE ${_TC_TEST_SOURCES})
    set_source_files_properties(${_TC_TEST_SOURCES} PROPERTIES
        COMPILE_DEFINITIONS "TC_CORE_TEST_NAME=\"${_TC_NAME}\"")
    source_group("tests/${_TC_NAME}" FILES ${_TC_TEST_SOURCES})
    list(APPEND _TC_CORE_TEST_NAMES "${_TC_NAME}")
endforeach()
list(LENGTH _TC_CORE_TEST_NAMES _TC_CORE_TEST_COUNT)
message(STATUS "[${PROJECT_NAME}] ${_TC_CORE_TEST_COUNT} core tests: ${_TC_CORE_TEST_NAMES}")

# trusscliPresets (see its local.cmake): trusscli's project-generation sources
# and their include dir. The include dir goes on that test's source only.
if("trusscliPresets" IN_LIST _TC_CORE_TEST_NAMES)
    set(_TC_TRUSSCLI_SRC "${_TC_CORE_TESTS_DIR}/../../tools/src")
    get_filename_component(_TC_TRUSSCLI_SRC "${_TC_TRUSSCLI_SRC}" ABSOLUTE)
    target_sources(${PROJECT_NAME} PRIVATE
        "${_TC_TRUSSCLI_SRC}/ProjectGenerator.cpp"
        "${_TC_TRUSSCLI_SRC}/ProjectState.cpp"
        "${_TC_TRUSSCLI_SRC}/BuildSetup.cpp"
        "${_TC_TRUSSCLI_SRC}/IdeHelper.cpp"
        "${_TC_TRUSSCLI_SRC}/VsDetector.cpp"
    )
    set_property(SOURCE "${_TC_CORE_TESTS_DIR}/trusscliPresets/src/main.cpp"
        APPEND PROPERTY INCLUDE_DIRECTORIES "${_TC_TRUSSCLI_SRC}")
endif()
