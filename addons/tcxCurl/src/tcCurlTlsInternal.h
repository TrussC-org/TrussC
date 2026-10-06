#pragma once

#include <curl/curl.h>

namespace tcx::curl::detail {

constexpr long defaultSslOptions(bool customPem = false) {
    long options = 0;
#ifdef _WIN32
#ifdef CURLSSLOPT_NATIVE_CA
    // A custom PEM replaces the OS store rather than supplementing it.
    if (!customPem) options |= CURLSSLOPT_NATIVE_CA;
#endif
    // Revocation is best-effort, like browsers. If you need strict revocation
    // checking, please open an issue.
    // This also lets a custom certificate without a CRL/OCSP URL pass.
#ifdef CURLSSLOPT_REVOKE_BEST_EFFORT
    options |= CURLSSLOPT_REVOKE_BEST_EFFORT;
#endif
#endif
    (void)customPem;
    return options;
}

} // namespace tcx::curl::detail
