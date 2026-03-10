# =============================================================================
# tcxCurl - HTTPS Client Addon (libcurl + mbedTLS)
# =============================================================================

cmake_minimum_required(VERSION 3.20)
message(STATUS "[tcxCurl] Loading addon...")

add_library(tcxCurl INTERFACE)
target_include_directories(tcxCurl INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(EMSCRIPTEN)
    # WASM: use Emscripten Fetch API
    target_compile_definitions(tcxCurl INTERFACE TCX_HTTP_EMSCRIPTEN=1)
    target_link_options(tcxCurl INTERFACE -sFETCH=1)
else()
    include(FetchContent)

    # Fetch mbedTLS (if not already provided by another addon like tcxTls)
    if(NOT TARGET mbedtls)
        message(STATUS "[tcxCurl] Fetching mbedTLS...")
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
    endif()

    # Help curl's FindMbedTLS.cmake locate FetchContent-built mbedTLS
    FetchContent_GetProperties(mbedtls SOURCE_DIR MBEDTLS_SRC_DIR)
    set(MBEDTLS_INCLUDE_DIR "${MBEDTLS_SRC_DIR}/include" CACHE PATH "" FORCE)
    set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
    set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
    set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)

    # Fetch curl with mbedTLS backend
    message(STATUS "[tcxCurl] Fetching curl with mbedTLS backend...")
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBRTMP OFF CACHE BOOL "" FORCE)
    set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
    set(CURL_USE_GSASL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    set(USE_NGTCP2 OFF CACHE BOOL "" FORCE)
    set(CMAKE_DISABLE_FIND_PACKAGE_LDAP OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SECTRANSP OFF CACHE BOOL "" FORCE)
    set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        curl
        URL https://github.com/curl/curl/releases/download/curl-8_12_1/curl-8.12.1.tar.xz
        URL_HASH SHA256=0341f1ed97a26c811abaebd37d62b833956792b7607ea3f15d001613c76de202
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(curl)

    if(NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl ALIAS libcurl_static)
    endif()

    target_compile_definitions(tcxCurl INTERFACE TCX_HTTP_CURL=1)
    target_link_libraries(tcxCurl INTERFACE CURL::libcurl)
endif()

message(STATUS "[tcxCurl] Addon loaded")
