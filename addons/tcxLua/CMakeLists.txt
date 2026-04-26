# =============================================================================
# tcxLua - TrussC Lua binding addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0077 NEW)

set(ADDON_NAME tcxLua)
option(USE_LUAJIT "Enable building with LuaJIT" OFF)

if(USE_LUAJIT)
    message(STATUS "[${ADDON_NAME}] LuaJIT enabled")

    set(LUAJIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/LuaJIT)
    add_subdirectory(luajit-cmake)
    set(LUAJIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/LuaJIT) # WORKAROUND

    set(LUA_C_SOURCES "")
    set(LUA_CXX_SOURCES "")
    set(LUA_INCLUDE "")

    # set(LUA_HEADERS "")
    file(GLOB LUA_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/*.h)
else()
    message(STATUS "[${ADDON_NAME}] LuaJIT disabled (by default)")

    file(GLOB LUA_CXX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/*.cpp)
    file(GLOB LUA_C_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/*.c)
    file(GLOB LUA_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/*.h)
    file(GLOB LUA_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/lua/include/*.h)
endif()

file(GLOB BINGEN_GENERATED_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/*.cpp)
file(GLOB ADDON_INCLUDE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)
file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC
    ${ADDON_SOURCES}
    ${LUA_C_SOURCES}
    ${LUA_CXX_SOURCES}
    ${LUA_HEADERS}
    ${LUA_INCLUDE}
    ${ADDON_INCLUDE_HEADERS}
    ${ADDON_HEADERS}
    ${BINGEN_GENERATED_SOURCES}
)

if(USE_LUAJIT)
    target_compile_definitions(${ADDON_NAME} PRIVATE TCXLUA_USE_LUAJIT=1 SOL_LUAJIT=1)
    target_include_directories(${ADDON_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/luajit-header/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
else()
    target_include_directories(${ADDON_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/lua/include
    )
endif()

if(WIN32 AND MSVC)
    # MSVC on Windows: enable large object support to avoid
    # "too many sections" (/bigobj)
    target_compile_options(${ADDON_NAME} PRIVATE /bigobj)
endif()

if(USE_LUAJIT)
    target_link_libraries(${ADDON_NAME} PUBLIC TrussC luajit::lib luajit::header)
else()
    target_link_libraries(${ADDON_NAME} PUBLIC TrussC)
endif()