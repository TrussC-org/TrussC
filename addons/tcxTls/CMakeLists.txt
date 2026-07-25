# =============================================================================
# tcxTls - TLS/SSL Support Addon
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxTls)

# Fetch mbedTLS via FetchContent
include(FetchContent)
FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    # LTS branch. Keep on 3.6.x for security updates; pre-3.6 lacks several CVE
    # fixes (e.g. CVE-2024-23744, CVE-2024-28755). v3.6.3..v3.6.7 add many more
    # TLS/DTLS handshake, timing-side-channel and memory-safety fixes.
    GIT_TAG v3.6.7
)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(mbedtls)

# Embed the Mozilla CA bundle as a generated .cpp so a trust anchor fallback
# is available even when neither the OS trust store nor an explicit PEM is set.
# Refresh with addons/tcxTls/scripts/update_ca_bundle.sh.
set(_CA_BUNDLE_PEM "${CMAKE_CURRENT_SOURCE_DIR}/data/cacert.pem")
if(NOT EXISTS "${_CA_BUNDLE_PEM}")
    message(FATAL_ERROR "tcxTls: bundled cacert.pem missing at ${_CA_BUNDLE_PEM}. "
                        "Run addons/tcxTls/scripts/update_ca_bundle.sh.")
endif()
file(READ "${_CA_BUNDLE_PEM}" CA_BUNDLE_CONTENT)
# Parse out the Mozilla date line for logging.
string(REGEX MATCH "Certificate data from Mozilla[^\n]*" CA_BUNDLE_DATE "${CA_BUNDLE_CONTENT}")

# Convert PEM to a byte-array (hex) literal. MSVC enforces a 65535-byte limit
# on string literals (error C2026) which applies to raw strings as well, so
# the PEM is emitted as `unsigned char[]` instead.
file(READ "${_CA_BUNDLE_PEM}" _CA_BUNDLE_HEX HEX)
# Split each hex pair into `0x??,` and append `0x00` as null terminator.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _CA_BUNDLE_HEX "${_CA_BUNDLE_HEX}")
# Wrap ~16 bytes per line to keep the generated .cpp readable.
string(REGEX REPLACE "(0x[0-9a-f][0-9a-f],){16}" "\\0\n" _CA_BUNDLE_HEX "${_CA_BUNDLE_HEX}")
set(CA_BUNDLE_HEX "${_CA_BUNDLE_HEX}0x00")

set(_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_GEN_CPP "${_GEN_DIR}/tcTlsCaBundle.cpp")
file(MAKE_DIRECTORY "${_GEN_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tcTlsCaBundle.cpp.in"
    "${_GEN_CPP}"
    @ONLY
)

# Source files
file(GLOB ADDON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB ADDON_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS} ${_GEN_CPP})
add_library(tc::tcxTls ALIAS ${ADDON_NAME})

target_include_directories(${ADDON_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${ADDON_NAME} PUBLIC TrussC PRIVATE mbedtls mbedx509 mbedcrypto)
if(WIN32)
    # Needed for CertOpenSystemStoreW / CertEnumCertificatesInStore fallback.
    target_link_libraries(${ADDON_NAME} PRIVATE crypt32)
endif()
