#pragma once

#include <mbedtls/x509_crt.h>
#include <cstddef>
#include <vector>

namespace tcx::tls::tls_internal {

// Internal trust-anchor loading, shared by the Windows path and offline tests.
const char* bundledCaPem();
const char* bundledCaBundleDate();
size_t countCerts(const mbedtls_x509_crt* chain);

struct DefaultCaCounts {
    size_t windowsRoot = 0;
    size_t bundled = 0;
};

DefaultCaCounts loadWindowsDefaultCAs(
    mbedtls_x509_crt* chain,
    const std::vector<std::vector<unsigned char>>& osCertificates);

} // namespace tcx::tls::tls_internal
