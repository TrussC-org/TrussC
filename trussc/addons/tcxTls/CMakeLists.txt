# =============================================================================
# tcxTls - TLS/SSL サポートアドオン
# =============================================================================
# mbedTLS を使用した暗号化通信機能を提供
# =============================================================================

# mbedTLS 設定
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)

# mbedTLS をサブディレクトリとして追加
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/mbedtls/CMakeLists.txt")
    message(FATAL_ERROR "[tcxTls] mbedTLS not found. Please run:\n  git submodule add https://github.com/Mbed-TLS/mbedtls.git addons/tcxTls/libs/mbedtls")
endif()

add_subdirectory(libs/mbedtls)

# ライブラリ定義
add_library(tcxTls STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tcTlsClient.cpp
)

add_library(tc::tcxTls ALIAS tcxTls)

# インクルードパス
target_include_directories(tcxTls PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# mbedTLS をリンク（PRIVATE: ユーザーに公開しない）
target_include_directories(tcxTls PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/mbedtls/include
)

target_link_libraries(tcxTls
    PUBLIC tc::TrussC
    PRIVATE mbedtls mbedx509 mbedcrypto
)

message(STATUS "[tcxTls] TLS addon enabled with mbedTLS")
