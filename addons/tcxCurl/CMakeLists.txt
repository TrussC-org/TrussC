# =============================================================================
# tcxCurl - HTTPS Client Addon (libcurl; Schannel on Windows, system TLS else)
# =============================================================================

cmake_minimum_required(VERSION 3.20)
message(STATUS "[tcxCurl] Loading addon...")

if(ANDROID)
    # Android: no libcurl in the NDK. Use a JNI bridge to
    # java.net.HttpURLConnection — implementation lives in
    # src/tcxCurl_android.cpp + android/java/.../AndroidHttpClient.java.
    # Both are picked up automatically: the .cpp via target_sources here,
    # the .java by trussc_app.cmake which globs <addon>/android/java/.
    add_library(tcxCurl STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/tcxCurl_android.cpp)
    target_include_directories(tcxCurl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(tcxCurl PUBLIC TCX_HTTP_ANDROID=1)
    # JNI symbols come from libnativewindow / the Android runtime — no explicit
    # link needed; android/native_activity.h + jni.h ship in the NDK sysroot.
    # We pull sokol_app.h transitively via TrussC for sapp_android_get_native_activity().
    target_link_libraries(tcxCurl PUBLIC TrussC)
    message(STATUS "[tcxCurl] Addon loaded (Android / HttpURLConnection backend)")
    return()
endif()

add_library(tcxCurl INTERFACE)
target_include_directories(tcxCurl INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(EMSCRIPTEN)
    # WASM: use Emscripten Fetch API
    target_compile_definitions(tcxCurl INTERFACE TCX_HTTP_EMSCRIPTEN=1)
    target_link_options(tcxCurl INTERFACE -sFETCH=1)
elseif(NOT WIN32)
    # macOS/Linux: use system libcurl (SecureTransport / OpenSSL)
    find_package(CURL REQUIRED)
    target_compile_definitions(tcxCurl INTERFACE TCX_HTTP_CURL=1)
    target_link_libraries(tcxCurl INTERFACE CURL::libcurl)
else()
    # Windows: fetch curl from source, built against Schannel — the native
    # Windows TLS stack. Schannel uses the OS certificate store (no bundled CA
    # file to ship or let expire) and negotiates like the OS/browser, which is
    # the most interoperable choice against real servers. This puts Windows in
    # line with every other platform, which all use their system/OS TLS (macOS
    # SecureTransport, Linux system OpenSSL, Android HttpURLConnection, Web
    # fetch) — Windows is no longer the odd one out on a bundled TLS engine.
    #
    # (If a build ever needs a portable, bundled-CA TLS engine instead — e.g. a
    # locked-down offline kiosk with a stale OS root store — the tcxTls addon
    # already carries mbedTLS, so tcxCurl could opt into it here via
    # `_tc_load_addon(tcxTls)` + CURL_USE_MBEDTLS. Not built until a real need
    # exists: Schannel covers the normal case, and mbedTLS has shown weaker
    # real-world handshake interop.)
    include(FetchContent)

    # curlの共通設定
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
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SECTRANSP OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)

    # SSL backend: Schannel (Windows native), no mbedTLS.
    message(STATUS "[tcxCurl] Fetching curl with Schannel backend...")
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_MBEDTLS OFF CACHE BOOL "" FORCE)

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
