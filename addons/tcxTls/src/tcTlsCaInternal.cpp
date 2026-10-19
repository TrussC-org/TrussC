#include "tcTlsCaInternal.h"

#include <cstring>

namespace tcx::tls::tls_internal {

size_t countCerts(const mbedtls_x509_crt* chain) {
    size_t n = 0;
    for (const auto* cert = chain; cert != nullptr; cert = cert->next) {
        // An initialized, empty mbedTLS chain still has a head node.
        if (cert->raw.len != 0) ++n;
    }
    return n;
}

DefaultCaCounts loadWindowsDefaultCAs(
    mbedtls_x509_crt* chain,
    const std::vector<std::vector<unsigned char>>& osCertificates) {
    DefaultCaCounts counts;
    for (const auto& der : osCertificates) {
        if (mbedtls_x509_crt_parse_der(chain, der.data(), der.size()) == 0) {
            ++counts.windowsRoot;
        }
    }

    // Windows populates ROOT lazily. Always append the Mozilla bundle even
    // when the OS store already contributed certificates. Duplicates are OK.
    const size_t before = countCerts(chain);
    const char* pem = bundledCaPem();
    mbedtls_x509_crt_parse(chain, reinterpret_cast<const unsigned char*>(pem),
                          std::strlen(pem) + 1);
    counts.bundled = countCerts(chain) - before;
    return counts;
}

} // namespace tcx::tls::tls_internal
