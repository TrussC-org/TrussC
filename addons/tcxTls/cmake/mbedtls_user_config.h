// ECC self-test counters are process-global and are not thread-safe.
// Production handshakes need no self-test entry points or counters.
#undef MBEDTLS_SELF_TEST
