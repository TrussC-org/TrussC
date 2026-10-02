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

    // curl echoes a proxy taken from the environment, userinfo included, on
    // info lines like
    //   Uses proxy env variable https_proxy == 'http://user:secret@host:3128'
    //   Unsupported proxy syntax in 'http://user:secret@host': <reason>
    // Picking a password out of that is fragile (a raw '@' or quote in it, and
    // curl cuts info lines at about 2 KB and error lines at 255 chars), so when
    // the value has a '@' or the line was cut, the whole value is replaced. A
    // "Uses proxy env variable" line always ends with the closing quote, so one
    // that doesn't was cut, and so is one at curl's 2047-char cap. The
    // proxy host still shows on curl's "Connected to" line. no_proxy (a host
    // list) is left alone. eol excludes the trailing CR/LF.
    inline bool isProxyEchoLine(std::string_view line) {
        if (line.rfind("Unsupported proxy ", 0) == 0) return true;
        constexpr std::string_view uses = "Uses proxy env variable ";
        if (line.rfind(uses, 0) != 0) return false;
        std::string_view var = line.substr(uses.size());
        return var.rfind("no_proxy ", 0) != 0 && var.rfind("NO_PROXY ", 0) != 0;
    }

    inline std::string redactProxyEchoLine(std::string_view line, size_t eol) {
        size_t open = line.find('\'');
        if (open == std::string_view::npos || open >= eol) return std::string(line);
        bool usesLine = line.rfind("Unsupported proxy ", 0) != 0;
        size_t close = line.rfind('\'', eol - 1);
        // curl <= 8.12 cuts at 2047 chars (8.5 without "..."), so a quote in the
        // value can land exactly at the end; 8.13+ cuts at 2043 + "...".
        bool cut = (close == open) ||
                   (usesLine ? (line[eol - 1] != '\'' || eol >= 2047) : eol >= 255);
        size_t valueEnd = cut ? eol : close;
        std::string_view value = line.substr(open + 1, valueEnd - open - 1);
        if (!cut && value.find('@') == std::string_view::npos) {
            return std::string(line);  // no userinfo: keep it readable
        }
        std::string out(line.substr(0, open + 1));
        out += "<redacted>";
        out.append(line.substr(valueEnd));
        return out;
    }

    // curl names the user it authenticates as on an info line:
    //   Proxy auth using Basic with user 'name'
    //   Server auth using Basic with user 'name'
    // Some proxies take an API key as the user name, so everything from the
    // first quote to the end of the line is replaced (the name may contain a
    // quote, and curl may cut the line). The scheme stays readable.
    inline bool isAuthUserLine(std::string_view line) {
        return line.rfind("Proxy auth using ", 0) == 0 ||
               line.rfind("Server auth using ", 0) == 0;
    }

    inline std::string redactAuthUserLine(std::string_view line, size_t eol) {
        size_t open = line.find('\'');
        if (open == std::string_view::npos || open >= eol) {
            // No quote: hide everything after "auth using ".
            constexpr std::string_view using_ = "auth using";
            open = line.find(using_) + using_.size();
        }
        std::string out(line.substr(0, open + 1));
        out += "<redacted>";
        if (eol > open + 1 && line[eol - 1] == '\'') out += '\'';
        out.append(line.substr(eol));
        return out;
    }

    // Returns one line of curl's debug output with a credential value replaced
    // by <redacted>. Catches both header lines ("Authorization: ...") and the
    // info lines curl writes for HTTP/2 and HTTP/3 requests
    // ("[HTTP/2] [1] [authorization: ...]"): a name counts when it starts the
    // line or follows '[' or whitespace. Inside [...] the value runs to the
    // ']' curl puts at the end of the line (a value may contain ']' itself).
    // Also masks the password in curl's echo of an environment proxy, and the
    // user name on curl's "Proxy/Server auth using ..." lines.
    inline std::string redactCredentialLine(std::string_view line) {
        size_t eol = line.size();
        while (eol > 0 && (line[eol - 1] == '\n' || line[eol - 1] == '\r')) --eol;
        if (isProxyEchoLine(line.substr(0, eol))) {
            return redactProxyEchoLine(line, eol);
        }
        if (isAuthUserLine(line.substr(0, eol))) {
            return redactAuthUserLine(line, eol);
        }
        for (size_t pos = 0; pos < eol; ++pos) {
            if (pos > 0 && line[pos - 1] != '[' && line[pos - 1] != ' ' && line[pos - 1] != '\t') continue;
            size_t colon = 0;
            if (!credentialNameAt(line.substr(0, eol), pos, colon)) continue;
            size_t valueEnd = eol;
            if (pos > 0 && line[pos - 1] == '[' && line[eol - 1] == ']' && eol - 1 > colon) {
                valueEnd = eol - 1;
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

    // --- setVerbose() output, without libcurl ---
    // HttpClient::debugCallback maps curl_infotype to VerboseKind and writes
    // what formatVerbose() appends to stderr. Kept here, outside the
    // TCX_HTTP_CURL block, so tests can replay callback sequences.
    enum class VerboseKind { Text, HeaderIn, HeaderOut };

    // Per-request state (CURLOPT_DEBUGDATA). A large request header block can
    // reach the callback in several pieces, splitting a line (and a
    // credential value) between calls: the unterminated tail waits here
    // until the rest arrives, so every line is redacted whole.
    struct VerboseState {
        std::string pendingHeaderOut;
        // A tail was printed before its line was complete (see
        // flushPendingHeaderOut): the start of the next piece is the rest of
        // that line, possibly a credential value, and is hidden.
        bool maskNextLine = false;
    };

    // Appends a block of debug output, redacted, with the prefix on each line.
    inline void appendVerbose(std::string& out, const char* prefix, std::string_view block) {
        std::string text = redactCredentialHeaders(block);
        size_t pos = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            size_t next = (nl == std::string::npos) ? text.size() : nl + 1;
            out += prefix;
            out.append(text, pos, next - pos);
            pos = next;
        }
        if (!text.empty() && text.back() != '\n') out += '\n';
    }

    // A request header block that ended mid-line (curl caps very large header
    // output, or the request was restarted): append what there is, redacted,
    // say it was cut, and hide the rest of that line if it shows up later.
    inline void flushPendingHeaderOut(VerboseState& state, std::string& out, bool maskRest = true) {
        if (state.pendingHeaderOut.empty()) return;
        appendVerbose(out, "> ", state.pendingHeaderOut);
        out += "* (request header output ended mid-line)\n";
        state.pendingHeaderOut.clear();
        state.maskNextLine = maskRest;
    }

    // setVerbose() output for one debug callback, like `curl -v`: info text
    // (TLS handshake lines included), request and response headers, one
    // prefix per line, credentials redacted. Appends to out.
    inline void formatVerbose(VerboseState* state, VerboseKind kind, std::string_view chunk,
                              std::string& out) {
        const char* prefix = kind == VerboseKind::Text     ? "* "
                           : kind == VerboseKind::HeaderIn ? "< "
                                                           : "> ";
        if (kind == VerboseKind::Text && state) {
            // The request that a pending tail belonged to is over. After
            // "Connection died" curl may still flush that request's remaining
            // bytes, so the rest of the cut line stays hidden; by "Issue
            // another request" they are discarded.
            if (chunk.rfind("Connection died, retrying", 0) == 0) {
                flushPendingHeaderOut(*state, out, true);
            } else if (chunk.rfind("Issue another request to this URL", 0) == 0) {
                flushPendingHeaderOut(*state, out, false);
                state->maskNextLine = false;  // also when "Connection died" armed it
            }
        }
        if (kind == VerboseKind::Text && isProxyEchoLine(chunk)) {
            // One echo per chunk: a value containing a newline stays masked.
            // Only curl's own trailing '\n' is dropped; a CR/LF inside the
            // value must not move the end-of-line checks.
            size_t eol = chunk.size();
            if (eol > 0 && chunk[eol - 1] == '\n') --eol;
            appendVerbose(out, prefix, redactProxyEchoLine(chunk, eol));
            return;
        }
        if (kind == VerboseKind::Text && isAuthUserLine(chunk)) {
            // Same for the user name curl authenticates as: it is URL-decoded,
            // so it may contain a newline.
            size_t eol = chunk.size();
            if (eol > 0 && chunk[eol - 1] == '\n') --eol;
            appendVerbose(out, prefix, redactAuthUserLine(chunk, eol));
            return;
        }
        if (kind == VerboseKind::HeaderOut && state) {
            // Header pieces keep arriving after the response has started
            // (curl 8.7+ finishes sending them), so a tail is only flushed at
            // the end of the transfer or when curl restarts the request.
            std::string block = std::move(state->pendingHeaderOut);
            state->pendingHeaderOut.clear();
            block.append(chunk);
            if (state->maskNextLine) {
                size_t nl = block.find('\n');
                if (nl == std::string::npos) return;  // still inside that line
                block.replace(0, nl, "<redacted>");
                state->maskNextLine = false;
            }
            size_t lastNl = block.rfind('\n');
            if (lastNl == std::string::npos) {
                state->pendingHeaderOut = std::move(block);
                return;
            }
            state->pendingHeaderOut = block.substr(lastNl + 1);
            block.resize(lastNl + 1);
            appendVerbose(out, prefix, block);
            return;
        }
        appendVerbose(out, prefix, chunk);
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
    // are shown as <redacted>, and so is a proxy URL with user:password taken
    // from the environment (https_proxy etc.) and the user name on curl's
    // "Proxy auth using ..." / "Server auth using ..." lines. Only those are
    // masked: a credential an app puts elsewhere (another header, the URL
    // query, user:pass@ in the base URL) may still be printed.
    void setVerbose(bool v) { verbose_ = v; }

    // --- TLS ---

    // CA certificate(s) in PEM form for verifying the server, e.g. a device
    // that serves HTTPS with a self-signed certificate. When set, the server
    // certificate is checked against this PEM only, in place of the OS default
    // trust store; verification of the peer and the host name stays on. An
    // empty string means the OS default trust store (the default). Used by
    // request() and uploadFile(). If the libcurl build cannot take a PEM from
    // memory, the request fails with response.error. On Android a request
    // with a CA set fails with response.error for now.
    void setTlsCACertificate(const std::string& pem) { tlsCaPem_ = pem; }

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
    std::string tlsCaPem_;

#ifdef TCX_HTTP_CURL
    // TLS options shared by request() and uploadFile(). Returns false, with
    // response.error set, when the CA PEM cannot be applied.
    bool applyTlsOptions(CURL* curl, HttpResponse& response) const {
        if (tlsCaPem_.empty()) {
#if defined(_WIN32) && defined(CURLSSLOPT_NATIVE_CA)
            // Windows curl is built against Schannel, which already verifies against the
            // OS certificate store — so this is a harmless no-op today. Kept as belt-and-
            // suspenders: if the backend is ever swapped (e.g. an OpenSSL build), it makes
            // curl use the OS trust store instead of failing with "SSL connect error".
            curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif
            return true;
        }
#if LIBCURL_VERSION_NUM >= 0x074D00  // CURLOPT_CAINFO_BLOB: curl 7.77.0
        // The PEM is the only trust source: no CA file, CA directory or OS store.
        curl_blob blob;
        blob.data = const_cast<char*>(tlsCaPem_.data());
        blob.len = tlsCaPem_.size();
        blob.flags = CURL_BLOB_COPY;
        CURLcode rc = curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
        if (rc != CURLE_OK) {
            response.error = std::string("setTlsCACertificate: this libcurl cannot use a CA PEM from memory (") +
                             curl_easy_strerror(rc) + ")";
            return false;
        }
        rc = curl_easy_setopt(curl, CURLOPT_CAINFO, static_cast<char*>(nullptr));
        if (rc == CURLE_OK) {
            rc = curl_easy_setopt(curl, CURLOPT_CAPATH, static_cast<char*>(nullptr));
            // Schannel (and other backends without CA directories) reports
            // NOT_BUILT_IN here. There is no directory trust source to clear.
            if (rc == CURLE_NOT_BUILT_IN) rc = CURLE_OK;
        }
        if (rc != CURLE_OK) {
            response.error = std::string("setTlsCACertificate: cannot replace the default CA store (") +
                             curl_easy_strerror(rc) + ")";
            return false;
        }
#if defined(_WIN32) && defined(CURLSSLOPT_REVOKE_BEST_EFFORT)
        // No NATIVE_CA here: the PEM replaces the OS store. Revocation is
        // checked best-effort, so a certificate without a CRL/OCSP URL passes.
        rc = curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
        if (rc != CURLE_OK) {
            response.error = std::string("setTlsCACertificate: ") + curl_easy_strerror(rc);
            return false;
        }
#endif
        return true;
#else
        response.error = "setTlsCACertificate: needs libcurl 7.77.0 or newer (CURLOPT_CAINFO_BLOB)";
        return false;
#endif
    }

    // libcurl write callback
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto* response = static_cast<std::string*>(userp);
        size_t totalSize = size * nmemb;
        response->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    // setVerbose() output: detail::formatVerbose() builds the text, written
    // to stderr here.
    static int debugCallback(CURL*, curl_infotype type, char* data, size_t size, void* userp) {
        detail::VerboseKind kind;
        switch (type) {
            case CURLINFO_TEXT:       kind = detail::VerboseKind::Text; break;
            case CURLINFO_HEADER_IN:  kind = detail::VerboseKind::HeaderIn; break;
            case CURLINFO_HEADER_OUT: kind = detail::VerboseKind::HeaderOut; break;
            default: return 0;  // bodies and TLS records (not shown by CURLOPT_VERBOSE either)
        }
        std::string out;
        detail::formatVerbose(static_cast<detail::VerboseState*>(userp), kind,
                              std::string_view(data, size), out);
        writeVerbose(out);
        return 0;
    }

    // End of the transfer: a request header tail still waiting is printed.
    static void flushPendingHeaderOut(detail::VerboseState& state) {
        std::string out;
        detail::flushPendingHeaderOut(state, out);
        writeVerbose(out);
    }

    static void writeVerbose(const std::string& text) {
        if (!text.empty()) std::fwrite(text.data(), 1, text.size(), stderr);
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
    if (!applyTlsOptions(curl, response)) {
        curl_easy_cleanup(curl);
        return response;
    }
    if (followRedirects_) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    }
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
    detail::VerboseState verboseState;
    if (verbose_) {
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debugCallback);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &verboseState);
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
    flushPendingHeaderOut(verboseState);

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
    if (!applyTlsOptions(curl, response)) {
        curl_easy_cleanup(curl);
        return response;
    }
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
