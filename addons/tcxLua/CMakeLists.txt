# =============================================================================
# tcxLua - TrussC Lua binding addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxLua)
option(USE_LUAJIT "Enable building with LuaJIT" OFF)

if(USE_LUAJIT)
    message(FATAL_ERROR "USE_LUAJIT is not implemented now.")
    set(LUA_C_SOURCES "")
    set(LUA_CXX_SOURCES "")
    set(LUA_HEADERS "")
else()
    file(GLOB LUA_CXX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/*.cpp)
    file(GLOB LUA_C_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/*.c)
    file(GLOB LUA_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/*.h)
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
    ${ADDON_INCLUDE_HEADERS}
    ${ADDON_HEADERS}
    ${BINGEN_GENERATED_SOURCES}
)

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32 AND MSVC)
    # MSVC on Windows: enable large object support to avoid
    # "too many sections" (/bigobj)
    target_compile_options(${ADDON_NAME} PRIVATE /bigobj)
endif()

target_link_libraries(${ADDON_NAME} PUBLIC TrussC)
