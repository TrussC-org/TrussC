# =============================================================================
# tcxTls - TLS/SSL サポートアドオン
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxTls)

# mbedTLS を FetchContent で取得（libs/ に展開して共有）
set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)
include(FetchContent)
FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.5.0
)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(mbedtls)

# ソースファイル
file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})
add_library(tc::tcxTls ALIAS ${ADDON_NAME})

target_include_directories(${ADDON_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${ADDON_NAME} PUBLIC TrussC PRIVATE mbedtls mbedx509 mbedcrypto)
