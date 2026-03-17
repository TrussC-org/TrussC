#pragma once

// =============================================================================
// tcxCurl - HTTP client for TrussC
// =============================================================================
// Native: libcurl, WASM: Emscripten Fetch API
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
#include <vector>
#include <algorithm>
#include <functional>
#include <nlohmann/json.hpp>

#ifdef TCX_HTTP_CURL
#include <curl/curl.h>
#endif

namespace tcx {

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

    // Enable verbose curl logging to stderr (for debugging)
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

    // Upload file via multipart POST
    HttpResponse uploadFile(const std::string& path, const std::string& filePath);

private:
    std::string baseUrl_;
    std::vector<std::pair<std::string, std::string>> headers_;
    long timeoutSeconds_ = 30;
    bool verbose_ = false;

    HttpResponse request(const std::string& method, const std::string& path,
                         const std::string& body,
                         const std::string& contentType = "application/json");

#ifdef TCX_HTTP_CURL
    // libcurl write callback
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto* response = static_cast<std::string*>(userp);
        size_t totalSize = size * nmemb;
        response->append(static_cast<char*>(contents), totalSize);
        return totalSize;
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
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
    if (verbose_) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    // Set method
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
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

#endif

} // namespace tcx
