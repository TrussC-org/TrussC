#pragma once

// =============================================================================
// tcxCurl - HTTP client for TrussC
// =============================================================================
// Native: libcurl (macOS/Linux/Windows), Android: java.net.HttpURLConnection
// via JNI, WASM: Emscripten Fetch API (TODO).
//
// The public API (HttpClient / HttpResponse) is the same on every backend so
// consumers like tcxOpenStreetMap don't need any #ifdef. On Android there is
// no system libcurl in the NDK, so we go through the JVM's HTTP client; the
// JNI plumbing lives in HttpClient_android.cpp and we declare (not inline-
// define) request/uploadFile in this header for that platform only.
//
// Usage:
//   tcx::HttpClient client;
//   client.setBaseUrl("https://server:8080");
//
//   auto res = client.get("/api/photos");
//   if (res.ok()) {
//       auto data = res.json();
//   }
//
//   auto res = client.post("/api/import", json{{"path", "/file.ARW"}});
//
//   // Download binary (thumbnail, etc.)
//   auto res = client.get("/api/photos/photo_1/thumbnail");
//   if (res.ok()) {
//       auto& bytes = res.body;  // raw bytes
//   }
// =============================================================================

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <functional>
#include <cctype>
#include <cstdio>
#include <nlohmann/json.hpp>

#ifdef TCX_HTTP_CURL
#include <curl/curl.h>
#endif

namespace tcx::curl {

// HTTP response
struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::string error;

    bool ok() const { return statusCode >= 200 && statusCode < 300; }

    nlohmann::json json() const {
        if (body.empty()) return nlohmann::json::object();
        try {
            return nlohmann::json::parse(body);
        } catch (...) {
            return nlohmann::json::object();
        }
    }
};

// Process-wide curl init/cleanup (called once automatically)
namespace detail {
    struct CurlGlobalGuard {
        CurlGlobalGuard() {
#ifdef TCX_HTTP_CURL
            curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
        }
        ~CurlGlobalGuard() {
#ifdef TCX_HTTP_CURL
            curl_global_cleanup();
#endif
        }
    };
    inline void ensureCurlInit() {
        static CurlGlobalGuard guard;
    }

    // Credential headers: setVerbose() output shows their name but not their
    // value. Matched by name only (case-insensitive).
    inline bool credentialNameAt(std::string_view line, size_t pos, size_t& colon) {
        static constexpr std::string_view names[] = {
            "authorization", "proxy-authorization", "x-api-key", "api-key",
        };
        for (auto name : names) {
            if (line.size() - pos > name.size() && line[pos + name.size()] == ':' &&
                std::equal(name.begin(), name.end(), line.begin() + pos, [](char n, char c) {
                    return n == std::tolower(static_cast<unsigned char>(c));
                })) {
                colon = pos + name.size();
                return true;
            }
        }
        return false;
    }

    // Returns one line of curl's debug output with a credential value replaced
    // by <redacted>. Catches both header lines ("Authorization: ...") and the
    // info lines curl writes for HTTP/2 and HTTP/3 requests
    // ("[HTTP/2] [1] [authorization: ...]"): a name counts when it starts the
    // line or follows '[' or whitespace. Inside [...] the value ends at ']'.
    inline std::string redactCredentialLine(std::string_view line) {
        size_t eol = line.size();
        while (eol > 0 && (line[eol - 1] == '\n' || line[eol - 1] == '\r')) --eol;
        for (size_t pos = 0; pos < eol; ++pos) {
            if (pos > 0 && line[pos - 1] != '[' && line[pos - 1] != ' ' && line[pos - 1] != '\t') continue;
            size_t colon = 0;
            if (!credentialNameAt(line.substr(0, eol), pos, colon)) continue;
            size_t valueEnd = eol;
            if (pos > 0 && line[pos - 1] == '[') {
                size_t close = line.find(']', colon);
                if (close != std::string_view::npos && close < eol) valueEnd = close;
            }
            std::string out(line.substr(0, colon + 1));
            out += " <redacted>";
            out.append(line.substr(valueEnd));
            return out;
        }
        return std::string(line);
    }

    // Applies redactCredentialLine() to every line of a block, line endings kept.
    inline std::string redactCredentialHeaders(std::string_view block) {
        std::string out;
        out.reserve(block.size());
        size_t pos = 0;
        while (pos < block.size()) {
            size_t nl = block.find('\n', pos);
            size_t next = (nl == std::string_view::npos) ? block.size() : nl + 1;
            out += redactCredentialLine(block.substr(pos, next - pos));
            pos = next;
        }
        return out;
    }
} // namespace detail

// HTTP client
class HttpClient {
public:
    HttpClient() {
        detail::ensureCurlInit();
    }

    ~HttpClient() = default;

    // Set base URL (e.g. "https://server:8080")
    void setBaseUrl(const std::string& url) { baseUrl_ = url; }
    const std::string& getBaseUrl() const { return baseUrl_; }

    // --- Custom headers ---

    // Add a custom header
    void addHeader(const std::string& key, const std::string& value) {
        // Overwrite if key already exists
        for (auto& h : headers_) {
            if (h.first == key) { h.second = value; return; }
        }
        headers_.push_back({key, value});
    }

    // Set Bearer token for Authorization header
    void setBearerToken(const std::string& token) {
        if (token.empty()) {
            // Remove Authorization header
            headers_.erase(
                std::remove_if(headers_.begin(), headers_.end(),
                    [](const auto& h) { return h.first == "Authorization"; }),
                headers_.end());
        } else {
            addHeader("Authorization", "Bearer " + token);
        }
    }

    // Clear all custom headers
    void clearHeaders() { headers_.clear(); }

    // Set request timeout in seconds (default: 30)
    void setTimeout(long seconds) { timeoutSeconds_ = seconds; }

    // Follow 3xx redirects (default off). Needed for e.g. public file shares that
    // redirect the download to a signed URL. Off by default so existing callers
    // (which expect the raw response) are unaffected.
    void setFollowRedirects(bool follow) { followRedirects_ = follow; }

    // Enable verbose curl logging to stderr (for debugging). The values of
    // the Authorization, Proxy-Authorization, X-Api-Key and Api-Key headers
    // are shown as <redacted>. Only those header names are masked: a
    // credential an app puts elsewhere (another header, the URL query) is
    // printed as-is.
    void setVerbose(bool v) { verbose_ = v; }

    // Check if server is reachable
    bool isReachable() {
        auto res = get("/api/health");
        return res.ok();
    }

    // GET request
    HttpResponse get(const std::string& path) {
        return request("GET", path, "");
    }

    // POST request with JSON body
    HttpResponse post(const std::string& path, const nlohmann::json& data) {
        return request("POST", path, data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }

    // POST request with raw body
    HttpResponse postRaw(const std::string& path, const std::string& body,
                         const std::string& contentType = "application/octet-stream") {
        return request("POST", path, body, contentType);
    }

    // PUT request with raw body
    HttpResponse put(const std::string& path, const std::string& body,
                     const std::string& contentType = "application/octet-stream") {
        return request("PUT", path, body, contentType);
    }

    // DELETE request
    HttpResponse del(const std::string& path) {
        return request("DELETE", path, "");
    }

    // Generic request with an explicit HTTP/WebDAV method (GET, POST, PUT, DELETE,
    // MKCOL, PROPFIND, COPY, MOVE, ...). The typed helpers above wrap this.
    HttpResponse request(const std::string& method, const std::string& path,
                         const std::string& body = "",
                         const std::string& contentType = "application/json");

    // Upload file via multipart POST
    HttpResponse uploadFile(const std::string& path, const std::string& filePath);

private:
    std::string baseUrl_;
    std::vector<std::pair<std::string, std::string>> headers_;
    long timeoutSeconds_ = 30;
    bool followRedirects_ = false;
    bool verbose_ = false;

#ifdef TCX_HTTP_CURL
    // libcurl write callback
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto* response = static_cast<std::string*>(userp);
        size_t totalSize = size * nmemb;
        response->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    // setVerbose() output: what CURLOPT_VERBOSE prints (info text, request
    // and response headers), one prefix per line, credentials redacted.
    static int debugCallback(CURL*, curl_infotype type, char* data, size_t size, void*) {
        const char* prefix = nullptr;
        switch (type) {
            case CURLINFO_TEXT:       prefix = "* "; break;
            case CURLINFO_HEADER_IN:  prefix = "< "; break;
            case CURLINFO_HEADER_OUT: prefix = "> "; break;
            default: return 0;  // bodies and TLS records (not shown by CURLOPT_VERBOSE either)
        }
        std::string text = detail::redactCredentialHeaders(std::string_view(data, size));
        size_t pos = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            size_t next = (nl == std::string::npos) ? text.size() : nl + 1;
            std::fprintf(stderr, "%s%.*s", prefix, static_cast<int>(next - pos), text.data() + pos);
            pos = next;
        }
        if (!text.empty() && text.back() != '\n') std::fputc('\n', stderr);
        return 0;
    }
#endif
};

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

#ifdef TCX_HTTP_CURL

inline HttpResponse HttpClient::request(const std::string& method, const std::string& path,
                                        const std::string& body, const std::string& contentType) {
    HttpResponse response;
    std::string url = baseUrl_ + path;

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize curl";
        return response;
    }

    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds_);
    // No short CURLOPT_CONNECTTIMEOUT: curl's Schannel backend reuses the connect
    // deadline for mid-transfer TLS *renegotiation*, so a 10s connect timeout made
    // any request whose first response byte arrives after 10s (e.g. a ~13s OpenAI
    // image generation, where the server then requests renegotiation) fail with
    // "SSL/TLS connection timeout". CURLOPT_TIMEOUT already bounds the whole
    // operation, so the connect phase stays bounded without breaking renegotiation.
#if defined(_WIN32) && defined(CURLSSLOPT_NATIVE_CA)
    // Windows curl is built against Schannel, which already verifies against the
    // OS certificate store — so this is a harmless no-op today. Kept as belt-and-
    // suspenders: if the backend is ever swapped (e.g. an OpenSSL build), it makes
    // curl use the OS trust store instead of failing with "SSL connect error".
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif
    if (followRedirects_) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    }
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
    if (verbose_) {
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debugCallback);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    // Set method
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (method != "GET") {
        // Generic verb (MKCOL, PROPFIND, COPY, MOVE, ...) for WebDAV etc.
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    }

    // Build headers
    struct curl_slist* headers = nullptr;
    for (const auto& h : headers_) {
        std::string line = h.first + ": " + h.second;
        headers = curl_slist_append(headers, line.c_str());
    }
    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
        std::string ct = "Content-Type: " + contentType;
        headers = curl_slist_append(headers, ct.c_str());
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
        response.body = std::move(responseBody);
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

inline HttpResponse HttpClient::uploadFile(const std::string& path, const std::string& filePath) {
    HttpResponse response;
    std::string url = baseUrl_ + path;

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize curl";
        return response;
    }

    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);  // 10 min for large RAW files
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);

    // Custom headers
    struct curl_slist* headers = nullptr;
    for (const auto& h : headers_) {
        std::string line = h.first + ": " + h.second;
        headers = curl_slist_append(headers, line.c_str());
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    // Multipart form
    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, filePath.c_str());

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
        response.body = std::move(responseBody);
    }

    if (headers) curl_slist_free_all(headers);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);
    return response;
}

#elif defined(TCX_HTTP_EMSCRIPTEN)

// TODO: Emscripten Fetch API implementation
inline HttpResponse HttpClient::request(const std::string& method, const std::string& path,
                                        const std::string& body, const std::string& contentType) {
    HttpResponse response;
    response.error = "Emscripten HTTP not yet implemented";
    return response;
}

inline HttpResponse HttpClient::uploadFile(const std::string& path, const std::string& filePath) {
    HttpResponse response;
    response.error = "Emscripten upload not yet implemented";
    return response;
}

#elif defined(TCX_HTTP_ANDROID)

// Implementations live in src/tcxCurl_android.cpp — they use JNI to drive
// java.net.HttpURLConnection. Only declarations here so each TU links to the
// single out-of-line definition in the static library.

#endif

} // namespace tcx::curl

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::curl`. These
// silent aliases keep older code compiling under flat `tcx::HttpClient`.
// DEPRECATED — removed in v1.0.0. (No [[deprecated]] attribute: under the usual
// `using namespace tc;` it would warn on idiomatic unqualified use too. See
// README for migration.)
// -----------------------------------------------------------------------------
namespace tcx { using curl::HttpResponse; using curl::HttpClient; } // deprecated: remove at v1.0.0
