#pragma once

#include <curl/curl.h>

namespace tcx::curl::detail {

constexpr long defaultSslOptions() {
#ifdef _WIN32
    // Revocation is best-effort, like browsers. If you need strict revocation
    // checking, please open an issue.
    return static_cast<long>(CURLSSLOPT_NATIVE_CA | CURLSSLOPT_REVOKE_BEST_EFFORT);
#else
    return 0L;
#endif
}

inline void applyTlsDefaults(CURL* curl) {
#ifdef _WIN32
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, defaultSslOptions());
#else
    (void)curl;
#endif
}

} // namespace tcx::curl::detail
