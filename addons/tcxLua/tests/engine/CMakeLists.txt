# Small engine-only regressions; no TrussC/GPU or generated binding dependency.
cmake_minimum_required(VERSION 3.16)
project(luaLoadBounds LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
option(USE_LUAJIT "Test the bundled LuaJIT instead of PUC Lua" OFF)
get_filename_component(ADDON_DIR ../.. ABSOLUTE)
if(USE_LUAJIT)
    set(LUAJIT_DIR "${ADDON_DIR}/LuaJIT")
    add_subdirectory("${ADDON_DIR}/luajit-cmake" luajit-cmake)
    set(ENGINE_LIBRARY luajit::lib luajit::header)
else()
    file(GLOB LUA_SOURCES "${ADDON_DIR}/lua/src/*.c")
    add_library(testLua STATIC ${LUA_SOURCES})
    target_include_directories(testLua PUBLIC "${ADDON_DIR}/lua/src" "${ADDON_DIR}/lua/include")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # Same as the addon: loslib.c only (mkstemp instead of tmpnam).
        set_source_files_properties("${ADDON_DIR}/lua/src/loslib.c"
            PROPERTIES COMPILE_DEFINITIONS LUA_USE_POSIX)
    endif()
    if(UNIX)
        target_link_libraries(testLua PUBLIC m ${CMAKE_DL_LIBS})
    endif()
    set(ENGINE_LIBRARY testLua)
endif()
add_executable(luaLoadBounds main.cpp "${ADDON_DIR}/src/tcxLuaLint.cpp")
target_include_directories(luaLoadBounds PRIVATE "${ADDON_DIR}/src")
target_link_libraries(luaLoadBounds PRIVATE ${ENGINE_LIBRARY})
if(USE_LUAJIT)
    target_compile_definitions(luaLoadBounds PRIVATE TCXLUA_USE_LUAJIT=1)
endif()
if(MSVC)
    target_compile_options(luaLoadBounds PRIVATE /utf-8)
endif()
if(EMSCRIPTEN)
    target_compile_options(luaLoadBounds PRIVATE -fexceptions)
    target_link_options(luaLoadBounds PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sABORTING_MALLOC=0 -sSTACK_SIZE=1048576)
endif()
if(NOT USE_LUAJIT)
    # Configure with -DCMAKE_C_FLAGS=-fsanitize=address and
    # -DCMAKE_CXX_FLAGS=-fsanitize=address to check the lookup/GC lifetime.
    add_executable(lintGlobalLifetime lintGlobalLifetime.cpp "${ADDON_DIR}/src/tcxLuaLint.cpp")
    target_include_directories(lintGlobalLifetime PRIVATE "${ADDON_DIR}/src")
    target_link_libraries(lintGlobalLifetime PRIVATE ${ENGINE_LIBRARY})
    if(EMSCRIPTEN)
        target_compile_options(lintGlobalLifetime PRIVATE -fexceptions)
        target_link_options(lintGlobalLifetime PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sABORTING_MALLOC=0)
    endif()
endif()
if(NOT USE_LUAJIT)
    # Model MSVC's throwing std::set move constructor on every STL. The
    # generated test implementation exercises the real scan container under ASan.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ADDON_DIR}/src/tcxLuaLint.cpp")
    file(READ "${ADDON_DIR}/src/tcxLuaLint.cpp" LINT_MODEL)
    string(REPLACE "using Names = std::set<std::string, std::less<>>;"
        "struct Names : std::set<std::string, std::less<>> {
    using Base = std::set<std::string, std::less<>>;
    using Base::Base;
    Names() = default;
    Names(const Names&) = default;
    Names(Names&& other) noexcept(false) : Base(std::move(other)) {}
};
static_assert(!std::is_nothrow_move_constructible_v<Names>);" LINT_MODEL "${LINT_MODEL}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lintSetMoveModel.cpp" "#include <type_traits>\n${LINT_MODEL}")
    add_executable(lintScanLifetime lintScanLifetime.cpp "${CMAKE_CURRENT_BINARY_DIR}/lintSetMoveModel.cpp")
    target_include_directories(lintScanLifetime PRIVATE "${ADDON_DIR}/src")
    target_link_libraries(lintScanLifetime PRIVATE ${ENGINE_LIBRARY})
    if(EMSCRIPTEN)
        target_compile_options(lintScanLifetime PRIVATE -fexceptions)
        target_link_options(lintScanLifetime PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sABORTING_MALLOC=0)
    endif()
endif()
