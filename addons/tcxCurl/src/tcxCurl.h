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
#include <chrono>
#include <climits>
#include <nlohmann/json.hpp>
#include "tcHttpResponseBounds.h"
#include "tc/utils/tcAnnotations.h"

#ifdef TCX_HTTP_CURL
#include "tcCurlTlsInternal.h"
#endif

namespace tcx::curl {

// HTTP response
struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::string error;

    bool ok() const { return statusCode >= 200 && statusCode < 300; }

    // Parse within 16 MiB input, depth 64 and 1,000,000 nodes (including keys),
    // plus an internal 128 MiB working budget that can reject earlier.
    // Budget rejection returns an empty object and logs one first-loss Warning
    // shared by JSON parsing across clients. Empty/invalid JSON also returns an
    // empty object, without a budget Warning unless a budget was exceeded.
    nlohmann::json json() const {
        return detail::parseResponseJson(body);
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

    // Shared by verbose redaction and cross-origin redirect filtering.
    // Keep AndroidHttpClient.CREDENTIAL_HEADERS mirrored with this list.
    inline constexpr std::string_view credentialHeaders[] = {
        "Authorization", "Proxy-Authorization", "X-Api-Key", "Api-Key", "X-Auth-Token", "Cookie",
    };
    inline bool asciiWhitespace(unsigned char c) {
        return c == ' ' || (c >= '\t' && c <= '\r');
    }
    // Extract from the original authority, before any URL parser can decode it.
    // False means there is no explicit, unambiguous authority to compare.
    inline bool rawUrlZone(std::string_view url, std::string_view& zone) {
        zone = {};
        size_t start = 0;
        if (url.substr(0, 2) != "//") {
            const auto scheme = url.find("://");
            if (scheme == std::string_view::npos) return false;
            start = scheme + 1;
        }
        start += 2;
        auto authority = url.substr(start, url.find_first_of("/?#", start) - start);
        const auto user = authority.rfind('@');
        if (user != std::string_view::npos) authority.remove_prefix(user + 1);
        if (authority.empty()) return false;
        if (authority.front() != '[') return true;
        const auto end = authority.find(']');
        if (end == std::string_view::npos) return false;
        const auto literal = authority.substr(1, end - 1);
        const auto scope = literal.find('%');
        if (scope == std::string_view::npos) return true;
        const auto separator = literal.substr(scope, 3) == "%25" ? 3 : 1;
        zone = literal.substr(scope + separator);
        return !zone.empty();
    }
    inline bool relativeUrlReference(std::string_view url) {
        if (url.empty() || url.substr(0, 2) == "//") return false;
        return url.substr(0, url.find_first_of("/?#")).find(':') == std::string_view::npos;
    }
    inline std::string_view trimHeaderName(std::string_view name) {
        while (!name.empty() && asciiWhitespace(name.front())) name.remove_prefix(1);
        while (!name.empty() && asciiWhitespace(name.back())) name.remove_suffix(1);
        return name;
    }
    inline bool validHeaderName(std::string_view name) {
        // RFC 9110 field-name = token; only ASCII tchar is permitted.
        return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
        });
    }
    inline bool validHeaderValue(std::string_view value) {
        return value.find_first_of(std::string_view("\r\n\0", 3)) == std::string_view::npos;
    }
    inline bool equalHeaderName(std::string_view a, std::string_view b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
            // HTTP names and DNS hosts use ASCII case folding, independent of locale.
            const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
            return lower(x) == lower(y);
        });
    }
    inline bool isCredentialHeader(std::string_view name) {
        name = trimHeaderName(name);
        return std::any_of(std::begin(credentialHeaders), std::end(credentialHeaders),
            [&](auto candidate) { return equalHeaderName(name, candidate); });
    }
    inline bool credentialNameAt(std::string_view line, size_t pos, size_t& colon) {
        colon = line.find(':', pos);
        return colon != std::string_view::npos && isCredentialHeader(line.substr(pos, colon - pos));
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

    // Add a custom header. Names must be valid HTTP field names; surrounding
    // ASCII whitespace is ignored. Values must not contain CR, LF or NUL.
    // An invalid header fails the request before anything is sent: statusCode 0,
    // empty body and response.error set. Names and values share one Warning per
    // client on the first invalid-header request. The Web stub sends nothing.
    void addHeader(const std::string& key, const std::string& value) {
        // Overwrite if key already exists
        for (auto& h : headers_) {
            if (h.first == key) { h.second = value; return; }
        }
        headers_.push_back({key, value});
    }

    // Set Bearer token for Authorization header. CR, LF or NUL fails the request
    // before I/O, using the same per-client Warning gate as invalid app headers.
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

    // Redirects are followed by default on every backend (v0.8 change); false
    // returns the 3xx itself. The Web backend is still a stub.
    // Follow at most 5 redirects, to http/https only, with no https->http downgrade.
    // On a different origin (scheme, host, port, IPv6 zone), omit Authorization,
    // Proxy-Authorization, X-Api-Key, Api-Key, X-Auth-Token and Cookie for the
    // remaining hops of this request.
    void setFollowRedirects(bool follow) { followRedirects_ = follow; }

    // Per-client response limit, default 64 MiB (67,108,864 bytes), for the whole
    // request including redirect hops and upload replies. Applies to decoded
    // bytes and independently to wire bytes. No effect on the Web stub.
    // Bound: a server controls response size. Exceeding the limit fails the whole
    // response: statusCode 0, empty body, error set; one first-loss Warning shared
    // across clients. Zero is ignored, preserving the limit, with a once Warning.
    // Storage grows only with received data. Initialized payload peaks at about
    // one response on desktop, twice on Android; desktop reserved capacity can
    // temporarily approach twice the response during final copying.
    // No stream-to-file API yet. Android multipart upload remains unsupported.
    TC_PLATFORMS("macos,windows,linux,android,web")
    void setResponseLimit(size_t bytes) {
        if (!bytes) { detail::reportInvalidResponseLimit(); return; }
        responseLimit_ = bytes;
    }

    // Enable verbose curl logging to stderr (for debugging). The values of
    // the Authorization, Proxy-Authorization, X-Api-Key, Api-Key,
    // X-Auth-Token and Cookie headers
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
    // A sent Content-Type value follows addHeader's CR/LF/NUL rejection rule.
    HttpResponse request(const std::string& method, const std::string& path,
                         const std::string& body = "",
                         const std::string& contentType = "application/json");

    // Upload file via multipart POST
    HttpResponse uploadFile(const std::string& path, const std::string& filePath);

private:
    std::string baseUrl_;
    std::vector<std::pair<std::string, std::string>> headers_;
    long timeoutSeconds_ = 30;
    bool followRedirects_ = true;
    size_t responseLimit_ = detail::responseByteLimit;
    bool verbose_ = false;
    std::string tlsCaPem_;
    mutable detail::RequestHeaderWarning invalidHeaderWarned_; // shared by invalid names/values

    // Canonicalize once as app headers become request headers, before backend I/O.
    bool prepareHeaders(std::vector<std::pair<std::string, std::string>>& result, HttpResponse& response,
                        const std::string* contentType = nullptr) const {
        if (contentType && !detail::validHeaderValue(*contentType)) {
            response.error = "Invalid HTTP header value";
            detail::reportInvalidRequestHeader(invalidHeaderWarned_, "Invalid HTTP header value");
            return false;
        }
        for (const auto& h : headers_) {
            const auto name = detail::trimHeaderName(h.first);
            const char* invalid = !detail::validHeaderName(name) ? "Invalid HTTP header name" :
                !detail::validHeaderValue(h.second) ? "Invalid HTTP header value" : nullptr;
            if (invalid) {
                response.error = invalid;
                // Do not log the app's header name or value (it may be a credential).
                detail::reportInvalidRequestHeader(invalidHeaderWarned_, invalid);
                return false;
            }
            result.emplace_back(std::string(name), h.second);
        }
        return true;
    }

#ifdef TCX_HTTP_CURL
    // TLS options shared by request() and uploadFile(). Returns false, with
    // response.error set, when a TLS option cannot be applied.
    bool applyTlsOptions(CURL* curl, HttpResponse& response) const {
        const long sslOptions = detail::defaultSslOptions(!tlsCaPem_.empty());
        if (sslOptions != 0) {
            const CURLcode rc = curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, sslOptions);
            if (rc != CURLE_OK) {
                response.error = std::string(tlsCaPem_.empty() ? "TLS options: " : "setTlsCACertificate: ") +
                                 curl_easy_strerror(rc);
                return false;
            }
        }
        if (tlsCaPem_.empty()) return true;
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
        return true;
#else
        response.error = "setTlsCACertificate: needs libcurl 7.77.0 or newer (CURLOPT_CAINFO_BLOB)";
        return false;
#endif
    }

    // libcurl write callback
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto* response = static_cast<detail::ResponseBuffer*>(userp);
        if (size != 0 && nmemb > SIZE_MAX / size) {
            response->reject(detail::ResponseLoss::Body, SIZE_MAX);
            return 0;
        }
        const size_t count = size * nmemb;
        return response->append(static_cast<const char*>(contents), count) ? count : 0;
    }

    static size_t headerCallback(char* data, size_t size, size_t count, void* userp) {
        auto& response = *static_cast<detail::ResponseBuffer*>(userp);
        if (size && count > SIZE_MAX / size) {
            response.reject(detail::ResponseLoss::Metadata, SIZE_MAX);
            return 0;
        }
        const size_t bytes = size * count;
        if (response.expectStatus && !response.bodyStarted) {
            // libcurl emits this after an outgoing request, or a 1xx block.
            // CURLINFO_RESPONSE_CODE supplies the parsed status independently.
            long code = 0;
            curl_easy_getinfo(static_cast<CURL*>(response.easy), CURLINFO_RESPONSE_CODE, &code);
            const bool interim = response.status >= 100 && response.status < 200;
            // A late debug piece alone cannot reopen a completed header block.
            // Require parsed status/redirect progress, or an interim response.
            if (!response.headersEnded || interim || code != response.status) {
                response.clear();
                response.status = static_cast<int>(code);
                response.headersEnded = false;
            }
            response.expectStatus = false;
        }
        const bool accepted = response.header(data, bytes);
        if (accepted && !response.headersEnded) {
            const std::string_view line(data, bytes);
            if (response.locationSeen && !line.empty() && (line.front() == ' ' || line.front() == '\t'))
                response.ambiguousLocation = true;
            const auto colon = line.find(':');
            if (colon != std::string_view::npos && detail::equalHeaderName(line.substr(0, colon), "Location")) {
                if (response.locationSeen) response.ambiguousLocation = true;
                response.locationSeen = true;
                try { response.rawLocation = detail::trimHeaderName(line.substr(colon + 1)); }
                catch (...) { response.reject(detail::ResponseLoss::Metadata, detail::metadataByteLimit + 1); return 0; }
                if (response.rawLocation.empty()) response.ambiguousLocation = true;
            }
        }
        if (bytes == 2 && data[0] == '\r' && data[1] == '\n') {
            response.headersEnded = true;
            if (response.status >= 100 && response.status < 200) response.expectStatus = true;
        }
        return accepted ? bytes : 0;
    }
    static int progressCallback(void* userp, curl_off_t, curl_off_t downloaded,
                                curl_off_t, curl_off_t) {
        auto& response = *static_cast<detail::ResponseBuffer*>(userp);
        if (downloaded > 0) {
            if (static_cast<uint64_t>(downloaded) > response.limit - response.wireTotal)
                response.reject(detail::ResponseLoss::Body, response.limit == SIZE_MAX ? response.limit : response.limit + 1);
            else response.hopWire = (std::max)(response.hopWire, static_cast<size_t>(downloaded));
        }
        return response.rejected ? 1 : 0;
    }
    static bool configureResponse(CURL* curl, detail::ResponseBuffer& response) {
        response.easy = curl;
        if (curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, response.backendError) != CURLE_OK) return false;
        if (curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debugCallback) != CURLE_OK) return false;
        if (curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &response) != CURLE_OK) return false;
        if (curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L) != CURLE_OK) return false;
        if (curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback) != CURLE_OK) return false;
        if (curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response) != CURLE_OK) return false;
        if (curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback) != CURLE_OK) return false;
        if (curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response) != CURLE_OK) return false;
        if (curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback) != CURLE_OK) return false;
        if (curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &response) != CURLE_OK) return false;
        if (curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L) != CURLE_OK) return false;
        // curl decodes incrementally, so the write callback bounds expanded
        // bytes, independently of the wire-byte progress callback.
        if (curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "") != CURLE_OK) return false;
        // Each easy handle is destroyed after the request: DNS results, TLS
        // state and failed/redirected transfer storage cannot collect in a cache.
        if (curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 0L) != CURLE_OK) return false;
        return true;
    }
    static void finishResponse(CURL* curl, CURLcode result,
                               detail::ResponseBuffer& buffer, HttpResponse& response) {
        // A libcurl header ceiling can fire before HEADERFUNCTION. Its bounded
        // error buffer identifies that cause on newer versions. Older curl reports
        // OOM without a diagnostic for both a partial overlong line and an
        // allocation failure in headers: report that ambiguous backend reason
        // separately, never claim it is our metadata overflow.
        bool backendHeaderResourceFailure = result == CURLE_OUT_OF_MEMORY;
#if LIBCURL_VERSION_NUM >= 0x080600
        backendHeaderResourceFailure = backendHeaderResourceFailure || result == CURLE_TOO_LARGE;
#endif
        if (!buffer.rejected && result != CURLE_OK &&
            (std::strstr(buffer.backendError, "Too large response header") ||
             std::strstr(buffer.backendError, "Header overflow") ||
             (backendHeaderResourceFailure && !buffer.bodyStarted &&
              (buffer.metadata > 0 || buffer.requestIssued) &&
              (!buffer.headersEnded || buffer.expectStatus)))) {
            buffer.backendHeaderLimit = true;
        }
        buffer.report(); // after the C callbacks, on the synchronous caller
        if (buffer.rejected || result != CURLE_OK) {
            response.error = buffer.rejected ? (buffer.loss == detail::ResponseLoss::HeaderSyntax
                ? "Ambiguous HTTP response trailer" : buffer.loss == detail::ResponseLoss::RedirectPolicy
                ? "HTTP redirect policy refused target" : buffer.loss == detail::ResponseLoss::Redirects
                ? "HTTP redirect limit exceeded" : "HTTP response exceeded its memory limit") : curl_easy_strerror(result);
            return;
        }
        try {
            buffer.publish(response.body);
            long code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            response.statusCode = static_cast<int>(code);
        } catch (...) {
            response.body.clear();
            response.error = "HTTP response allocation failed";
        }
    }

    // Both normal requests and multipart replies use the same hop admission.
    void performHops(CURL* curl, detail::ResponseBuffer& buffer, HttpResponse& response,
                     std::string url, std::string method, const std::string& body,
                     const std::string& contentType, const std::string* filePath, long timeout,
                     const std::vector<std::pair<std::string, std::string>>& requestHeaders) {
        bool forwardCredentials = true, sendBody = true;
        const auto start = std::chrono::steady_clock::now();
        for (size_t redirects = 0;; ++redirects) {
            // Dispose per-hop storage; decoded/wire/metadata admission belongs to the request.
            buffer.clear();
            buffer.wireTotal += buffer.hopWire;
            buffer.hopWire = 0;
            buffer.expectStatus = true; buffer.headersEnded = false;
            buffer.status = 0; buffer.requestIssued = false; buffer.backendError[0] = 0;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            // The initial URL remains app-chosen, including file:// and ftp://.
            // Never enable CURLOPT_FOLLOWLOCATION: validate each server-chosen target below.
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, static_cast<char*>(nullptr));
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, static_cast<char*>(nullptr));
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(0));
            curl_easy_setopt(curl, CURLOPT_MIMEPOST, static_cast<curl_mime*>(nullptr));
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            // Rebuild multipart state per hop so file streams start at byte zero.
            curl_mime* mime = nullptr;
            if (sendBody && filePath) {
                mime = curl_mime_init(curl);
                curl_mimepart* part = mime ? curl_mime_addpart(mime) : nullptr;
                if (!part) {
                    curl_mime_free(mime);
                    response.error = "HTTP upload allocation failed"; return;
                }
                curl_mime_name(part, "file");
                curl_mime_filedata(part, filePath->c_str());
                curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
            } else if (sendBody && !body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
            }
            if (method == "HEAD") curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            else if (method == "POST") {
                // CURLOPT_POST would replace curl's multipart request mode.
                if (!(sendBody && mime)) curl_easy_setopt(curl, CURLOPT_POST, 1L);
            }
            // Preserve curl's default command behavior for app-chosen FTP URLs.
            else if (method != "GET" || (sendBody && !body.empty()))
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());

            struct curl_slist* headers = nullptr;
            for (const auto& h : requestHeaders) {
                if (!forwardCredentials && detail::isCredentialHeader(h.first)) continue;
                headers = curl_slist_append(headers, (h.first + ": " + h.second).c_str());
            }
            if (sendBody && !body.empty())
                headers = curl_slist_append(headers, ("Content-Type: " + contentType).c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            const int64_t timeoutMs = timeout <= 0 ? 0 : timeout > INT64_MAX / 1000 ? INT64_MAX : static_cast<int64_t>(timeout) * 1000;
            const int64_t remaining = timeoutMs - elapsed;
            if (timeout > 0) curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                static_cast<long>((std::clamp)(remaining, int64_t{1}, static_cast<int64_t>(LONG_MAX))));
            CURLcode result = timeout > 0 && remaining <= 0 ? CURLE_OPERATION_TIMEDOUT : curl_easy_perform(curl);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, static_cast<curl_slist*>(nullptr));
            curl_slist_free_all(headers);
            curl_easy_setopt(curl, CURLOPT_MIMEPOST, static_cast<curl_mime*>(nullptr));
            curl_mime_free(mime);
            if (verbose_) flushPendingHeaderOut(*static_cast<detail::VerboseState*>(buffer.verboseState));
            if (result != CURLE_OK || buffer.rejected || !followRedirects_) {
                finishResponse(curl, result, buffer, response); return;
            }
            long status = 0;
            char* redirectUrl = nullptr;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirectUrl);
            if (!redirectUrl || !(status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
                finishResponse(curl, result, buffer, response); return;
            }
            // Bound (security): peer redirect chains retain metadata and connection state. Overflow rejects the whole response and is counted.
            if (redirects == 5) {
                buffer.reject(detail::ResponseLoss::Redirects, 6);
                finishResponse(curl, CURLE_OK, buffer, response); return;
            }
            const size_t bytes = std::strlen(redirectUrl);
            if (bytes > detail::metadataByteLimit - buffer.metadata) {
                buffer.reject(detail::ResponseLoss::Metadata, detail::metadataByteLimit + 1);
                finishResponse(curl, CURLE_OK, buffer, response); return;
            }
            buffer.metadata += bytes;
            std::string next(redirectUrl);
            auto origin = [](const std::string& value, std::string& scheme, std::string& host, std::string& zone, std::string& port) {
                CURLU* parsed = curl_url();
                if (!parsed) return false;
                bool ok = curl_url_set(parsed, CURLUPART_URL, value.c_str(), CURLU_NON_SUPPORT_SCHEME) == CURLUE_OK;
                auto part = [&](CURLUPart key, std::string& out, unsigned flags = 0) {
                    char* text = nullptr;
                    if (curl_url_get(parsed, key, &text, flags) != CURLUE_OK) return false;
                    out = text; curl_free(text); return true;
                };
                ok = ok && part(CURLUPART_SCHEME, scheme) && part(CURLUPART_HOST, host) && part(CURLUPART_PORT, port, CURLU_DEFAULT_PORT);
                // HOST excludes the scope. ZONEID can already be decoded by
                // curl (including with flags 0); verify it against raw input below.
                char* scope = nullptr;
                const auto zoneResult = curl_url_get(parsed, CURLUPART_ZONEID, &scope, 0);
                if (zoneResult == CURLUE_OK) { zone = scope; curl_free(scope); }
                else if (zoneResult != CURLUE_NO_ZONEID) ok = false;
                curl_url_cleanup(parsed);
                return ok;
            };
            std::string scheme, host, zone, port, nextScheme, nextHost, nextZone, nextPort;
            // Bound (security): servers choose redirect schemes and TLS transitions. Overflow refuses non-HTTP(S) targets or HTTPS downgrades and is counted.
            char* effectiveUrl = nullptr;
            curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
            if (!effectiveUrl || !origin(effectiveUrl, scheme, host, zone, port) || !origin(next, nextScheme, nextHost, nextZone, nextPort) ||
                (nextScheme != "http" && nextScheme != "https") || (scheme == "https" && nextScheme == "http")) {
                buffer.reject(detail::ResponseLoss::RedirectPolicy, 1);
                finishResponse(curl, CURLE_OK, buffer, response); return;
            }
            // EFFECTIVE_URL and REDIRECT_URL are curl-built and can lose escapes.
            // Keep the app/hop input and the callback's Location as raw evidence.
            std::string_view rawZone, rawNextZone;
            const bool currentKnown = detail::rawUrlZone(url, rawZone);
            bool nextKnown = detail::rawUrlZone(buffer.rawLocation, rawNextZone);
            if (!nextKnown && detail::relativeUrlReference(buffer.rawLocation)) {
                rawNextZone = rawZone;
                nextKnown = currentKnown;
            }
            const bool exactZones = currentKnown && nextKnown && !buffer.ambiguousLocation &&
                rawZone == rawNextZone && rawZone == zone && rawNextZone == nextZone &&
                rawZone.find('%') == std::string_view::npos && rawNextZone.find('%') == std::string_view::npos;
            if (scheme != nextScheme || !detail::equalHeaderName(host, nextHost) || port != nextPort ||
                !exactZones)
                forwardCredentials = false;
            if (((status == 301 || status == 302) && method == "POST") || (status == 303 && method != "HEAD")) {
                method = "GET"; sendBody = false;
            }
            url = std::move(next);
        }
    }

    // setVerbose() output: detail::formatVerbose() builds the text, written
    // to stderr here.
    static int debugCallback(CURL*, curl_infotype type, char* data, size_t size, void* userp) {
        auto& response = *static_cast<detail::ResponseBuffer*>(userp);
        if (type == CURLINFO_HEADER_OUT) response.requestIssued = true;
        // Outgoing headers can arrive in multiple debug pieces, including
        // after the peer has responded. Only arm a new response for an initial
        // request or a status for which curl can actually retry/follow.
        if (type == CURLINFO_HEADER_OUT && !response.bodyStarted &&
            (response.status == 0 || (response.status >= 300 && response.status < 400) ||
             response.status == 401 || response.status == 407)) response.expectStatus = true;
        if (!response.verbose) return 0;
        detail::VerboseKind kind;
        switch (type) {
            case CURLINFO_TEXT:       kind = detail::VerboseKind::Text; break;
            case CURLINFO_HEADER_IN:  kind = detail::VerboseKind::HeaderIn; break;
            case CURLINFO_HEADER_OUT: kind = detail::VerboseKind::HeaderOut; break;
            default: return 0;  // bodies and TLS records (not shown by CURLOPT_VERBOSE either)
        }
        std::string out;
        detail::formatVerbose(static_cast<detail::VerboseState*>(response.verboseState), kind,
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

    std::vector<std::pair<std::string, std::string>> requestHeaders;
    if (!prepareHeaders(requestHeaders, response, body.empty() ? nullptr : &contentType)) return response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize curl";
        return response;
    }

    detail::ResponseBuffer responseBody(responseLimit_);
    responseBody.verbose = verbose_;
    detail::VerboseState verboseState;
    responseBody.verboseState = &verboseState;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (!configureResponse(curl, responseBody)) {
        response.error = "HTTP response bounds are unsupported by this libcurl";
        curl_easy_cleanup(curl);
        return response;
    }
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
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);

    performHops(curl, responseBody, response, url, method, body, contentType, nullptr, timeoutSeconds_, requestHeaders);

    curl_easy_cleanup(curl);
    return response;
}

inline HttpResponse HttpClient::uploadFile(const std::string& path, const std::string& filePath) {
    HttpResponse response;
    std::string url = baseUrl_ + path;

    std::vector<std::pair<std::string, std::string>> requestHeaders;
    if (!prepareHeaders(requestHeaders, response)) return response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize curl";
        return response;
    }

    detail::ResponseBuffer responseBody(responseLimit_);
    responseBody.verbose = verbose_;
    detail::VerboseState verboseState;
    responseBody.verboseState = &verboseState;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (!configureResponse(curl, responseBody)) {
        response.error = "HTTP response bounds are unsupported by this libcurl";
        curl_easy_cleanup(curl);
        return response;
    }
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);  // 10 min for large RAW files
    if (!applyTlsOptions(curl, response)) {
        curl_easy_cleanup(curl);
        return response;
    }
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);

    performHops(curl, responseBody, response, url, "POST", "", "", &filePath, 600L, requestHeaders);

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
