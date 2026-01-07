# =============================================================================
# tcxWebSocket - WebSocket Client Addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxWebSocket)

# Source files
file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})
add_library(tc::tcxWebSocket ALIAS ${ADDON_NAME})

target_include_directories(${ADDON_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Depends on TrussC and tcxTls (for wss:// on native)
# If tcxTls is not used, only ws:// will work on native.
if(EMSCRIPTEN)
    target_link_libraries(${ADDON_NAME} PUBLIC TrussC)
    target_link_options(${ADDON_NAME} PUBLIC "SHELL:-lwebsocket.js")
else()
    target_link_libraries(${ADDON_NAME} PUBLIC TrussC tc::tcxTls)
endif()
