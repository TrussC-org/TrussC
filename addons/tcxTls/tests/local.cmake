# The test runs its own TLS server on loopback, written against mbedTLS.
# tcxTls links mbedTLS privately and does not export its headers, so link the
# same targets (created by tcxTls's FetchContent) here as well.
target_link_libraries(${PROJECT_NAME} PRIVATE mbedtls mbedx509 mbedcrypto)
