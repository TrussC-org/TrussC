// =============================================================================
// tcMCP.cpp - MCP state that is one per process
// =============================================================================
// The tool / resource registry, the registration owner (#227), the status /
// status-image / alert registries, the deferred-reply bookkeeping and the HTTP
// server. All of it is shared between the frame loop (which serves requests)
// and app code (which registers tools), so it must be ONE instance per process.
//
// Defined here, non-inline, rather than as function-local statics in tcMCP.h /
// tcStandardTools.h: a hot reload guest on Windows is a DLL that compiles its
// own copy of every header-inline function, statics included, so the guest's
// tools landed in a registry the host never served (#249). A non-inline
// function lives in TrussC.lib, the host exports it, and the guest imports it:
// one instance on Windows too, as on Linux and macOS.
// See docs/ARCHITECTURE.md, "One instance per process".
// =============================================================================

#include <TrussC.h>
#include <deque>
#include <mutex>
#include <thread>
#include "tcMcpBuild.h"

#ifndef __EMSCRIPTEN__
#include "../../impl/httplib.h"
#ifndef _WIN32
#include <sys/socket.h>
#endif
#endif

namespace trussc {
namespace mcp {

namespace {
struct AdmissionState {
    std::mutex mutex;
    AdmissionLimits limits;
    std::atomic<size_t> connections{0};
    size_t screenshots = 0;
    std::atomic<uint64_t> rejected[static_cast<size_t>(internal::AdmissionStore::Count)]{};
};
AdmissionState& admissionState() {
    static auto* state = new AdmissionState;
    return *state;
}
}

AdmissionLimits getAdmissionLimits() {
    auto& state = admissionState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.limits;
}

bool setAdmissionLimits(const AdmissionLimits& limits) {
    auto& state = admissionState();
    const char* reason = nullptr;
    const size_t values[] = {limits.bodyBytes, limits.jsonDepth, limits.outstanding, limits.workers,
        limits.histogramBins,
        limits.gridCells, limits.operations};
    for (auto value : values) if (!value) { reason = "limits must be positive"; break; }
    {
        std::lock_guard<std::mutex> lock(state.mutex);
#ifndef __EMSCRIPTEN__
        if (!reason && internal::getHttpServer()) reason = "HTTP is running";
#endif
        if (!reason && (state.connections.load() || state.screenshots)) reason = "work is still outstanding";
        if (!reason) {
            state.limits = limits;
            return true;
        }
    }
    ++state.rejected[static_cast<size_t>(internal::AdmissionStore::Configuration)];
    // Log outside the mutex: listeners may query the retained limits.
    logWarning("MCP") << "Admission limits rejected: " << reason << "; configuration unchanged";
    return false;
}

namespace internal {
void admissionRejected(AdmissionStore store, size_t capacity, size_t needed, const char* units) {
    static constexpr const char* names[] = {"body", "connections", "histogram", "grid", "operations", "working memory", "headers", "request line", "configuration"};
    auto& count = admissionState().rejected[static_cast<size_t>(store)];
    if (count.fetch_add(1) == 0)
        logWarning("MCP") << names[static_cast<size_t>(store)] << " capacity " << capacity
                          << ", needed " << needed << " " << (units ? units :
                              (store == AdmissionStore::Body ||
                               store == AdmissionStore::Working || store == AdmissionStore::Headers ||
                               store == AdmissionStore::RequestLine ? "bytes" : "items"))
                          << "; request rejected";
}
uint64_t admissionRejections(AdmissionStore store) {
    return admissionState().rejected[static_cast<size_t>(store)].load();
}
size_t admittedConnections() { return admissionState().connections.load(); }
void checkAdmission(size_t needed, size_t capacity, AdmissionStore store) {
    // Bound (security): external parameters must fit their owning store before allocation. Overflow rejects the unit and is counted.
    if (needed > capacity) { admissionRejected(store, capacity, needed); throw AdmissionError(); }
}
WindowCaptureAdmission*& WindowCaptureAdmission::current() {
    static thread_local WindowCaptureAdmission* value = nullptr;
    return value;
}
std::shared_ptr<void> acquireScreenshotCredit() {
    struct Credit {
        bool owned = false;
        ~Credit() {
            if (!owned) return;
            auto& state = admissionState();
            std::lock_guard<std::mutex> lock(state.mutex);
            --state.screenshots;
        }
    };
    auto credit = std::make_shared<Credit>();
    auto& state = admissionState();
    size_t limit, needed;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        // Bound (security): deferred external captures share at most min(outstanding, 8) slots across windows. Overflow rejects the tool and counts a connections rejection.
        limit = std::min<size_t>(state.limits.outstanding, 8);
        needed = state.screenshots + 1;
        if (needed <= limit) {
            ++state.screenshots;
            credit->owned = true;
            return credit;
        }
    }
    admissionRejected(AdmissionStore::Connections, limit, needed);
    throw AdmissionError();
}


bool isDevBuild() {
#if TRUSSC_MCP_DEV && !defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}

bool& healthOnlyEndpoint() {
    static bool enabled = false;
    return enabled;
}

bool& explicitlyOpened() {
    static bool opened = false;
    return opened;
}

#ifndef __EMSCRIPTEN__
static bool mcpInitialized = false;
#endif

bool healthOnly() {
    return healthOnlyEndpoint() && getToolLevel() == ToolLevel::Public;
}

bool environmentClosesMcp() {
    const char* value = std::getenv("TRUSSC_MCP");
    return value && std::string_view(value) == "0";
}

bool environmentRequestsMcp() {
    const char* value = std::getenv("TRUSSC_MCP");
    return !environmentClosesMcp() &&
        ((value && std::string_view(value) == "1") || std::getenv("TRUSSC_MCP_PORT"));
}

void logExposure() {
#ifndef __EMSCRIPTEN__
    // setup() may register app/addon tools after granting access or binding.
    if (!mcpInitialized || !getHttpServer() || healthOnly()) return;
    const auto reply = json::parse(Server::instance().processMessage(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"));
    if (!reply.is_object() || !reply.contains("result") ||
        !reply["result"].is_object() || !reply["result"].contains("tools")) return;
    std::string names;
    for (const auto& tool : reply["result"]["tools"]) {
        if (!names.empty()) names += ", ";
        names += tool["name"].get<std::string>();
    }
    logNotice("MCP") << "ToolLevel " << toolLevelName(getToolLevel()) << ": " << names;
#endif
}

const std::string& mainWindowTitle(const std::string& title) {
    // buildAppDescriptor returns before sapp_run consumes the title pointer.
    // Keep its backing string in the host, including on hot reload builds.
    static std::string windowTitle;
    windowTitle = isDevBuild() && title.find("[MCP]") == std::string::npos
        ? title + " [MCP]" : title;
    return windowTitle;
}

void startConfiguredServer(const char* opener) {
#ifndef __EMSCRIPTEN__
    if (environmentClosesMcp()) {
        if (getHttpServer()) stopHttpServer();
        return;
    }
    const char* envPort = std::getenv("TRUSSC_MCP_PORT");
    const bool healthEndpoint = getToolLevel() == ToolLevel::Public;
    if (!isDevBuild() && healthEndpoint && !environmentRequestsMcp()) return;
    if (getHttpServer()) return;
    const char* host = isDevBuild() ? std::getenv("TRUSSC_MCP_HOST") : nullptr;
    const char* token = isDevBuild() ? std::getenv("TRUSSC_MCP_TOKEN") : nullptr;
    startHttpServerImpl(envPort ? std::atoi(envPort) : 0,
                        host ? host : "127.0.0.1", token ? token : "", healthEndpoint, opener);
    if (!getHttpServer()) return;
    if (healthEndpoint) {
        logNotice("MCP") << "MCP: health only (ToolLevel Public). The supervisor gets logs, restart and hang detection; for screenshots and status add mcp::setToolLevel(mcp::ToolLevel::ReadOnly) in setup().";
    }
#endif
}

void initializeMcp() {
#ifndef __EMSCRIPTEN__
    // App::onSetupDone also runs for replacement guests; startup is host state.
    if (mcpInitialized) return;
    mcpInitialized = true;
    if (isDevBuild())
        logWarning("MCP") << "MCP development build: Control tools are enabled. Do not distribute this build.";
    startConfiguredServer(environmentRequestsMcp() ? "the environment" : "the dev build");
    logExposure();
#endif
}

DeferralState& deferralState() {
    static DeferralState s;
    return s;
}

std::vector<DeferredResponse>& deferredResponses() {
    static auto* v = new std::vector<DeferredResponse>;
    return *v;
}

std::mutex& deferredResponsesMutex() {
    static auto* mutex = new std::mutex;
    return *mutex;
}

// Host storage: never reset by guest registration cleanup or a reload.
std::atomic<ToolLevel>& grantedToolLevel() {
    static std::atomic<ToolLevel> level{isDevBuild() ? ToolLevel::Control : ToolLevel::Public};
    return level;
}

const void*& registrationOwner() {
    static const void* owner = nullptr;
    return owner;
}

std::vector<std::function<void(const void*)>>& ownerCleanupHooks() {
    static std::vector<std::function<void(const void*)>> hooks;
    return hooks;
}

std::vector<StatusEntry>& statusRegistry() {
    static std::vector<StatusEntry> reg;
    static bool hooked = (hookOwnerCleanup(reg), true);
    (void)hooked;
    return reg;
}

std::vector<StatusImageEntry>& statusImageRegistry() {
    static std::vector<StatusImageEntry> reg;
    static bool hooked = (hookOwnerCleanup(reg), true);
    (void)hooked;
    return reg;
}

std::mutex& alertMutex() {
    static std::mutex m;
    return m;
}

std::deque<json>& alertQueue() {
    static std::deque<json> q;
    return q;
}

#ifndef __EMSCRIPTEN__

// Bounds header-section and chunk metadata lines; upstream parses, stores and routes requests.
class McpIngressStream final : public httplib::Stream {
public:
    explicit McpIngressStream(httplib::Stream& stream) : stream_(stream) {}
    bool is_readable() const override { return !overflow_ && stream_.is_readable(); }
    bool is_writable() const override { return stream_.is_writable(); }
    void get_remote_ip_and_port(std::string& ip, int& port) const override { stream_.get_remote_ip_and_port(ip, port); }
    void get_local_ip_and_port(std::string& ip, int& port) const override { stream_.get_local_ip_and_port(ip, port); }
    socket_t socket() const override { return stream_.socket(); }
    void body(const httplib::Request& req) {
        phase_ = httplib::detail::is_chunked_transfer_encoding(req.headers) ? Phase::ChunkSize : Phase::Body;
    }
    bool refused() const { return refused_ || overflow_; }
    bool overflowed() const { return overflow_; }
    ssize_t read(char* data, size_t size) override {
        if (overflow_) return -1;
        const auto n = stream_.read(data, size);
        if (n <= 0 || phase_ == Phase::Body) return n;
        if (phase_ == Phase::ChunkData && static_cast<size_t>(n) <= remaining_) {
            remaining_ -= static_cast<unsigned long>(n);
            if (!remaining_) phase_ = Phase::ChunkEnd;
            return n;
        }
        for (ssize_t i = 0; i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(data[i]);
            if (phase_ == Phase::Body) continue;
            if (phase_ == Phase::ChunkData) {
                if (--remaining_ == 0) phase_ = Phase::ChunkEnd;
                continue;
            }
            // Bound (network input): request lines stop at 8192 bytes before upstream line storage grows.
            if (phase_ == Phase::RequestLine && lineBytes_ == 8192) { overflow_ = requestLineOverflow_ = true; return -1; }
            // Bound (network input): chunk-size/extension lines stop at 1 KiB before upstream line storage grows.
            if (phase_ == Phase::ChunkSize && lineBytes_ == 1024) { overflow_ = true; return -1; }
            if (phase_ == Phase::Headers || phase_ == Phase::Trailers) {
                // Bound (security): upstream bounds individual lines, not raw aggregate bytes or field count before storage.
                // Initial headers and chunked trailers each get 64 KiB / 128 fields, including whitespace and CRLF bytes.
                const bool startsField = (lineBytes_ == 0 && c != '\r') ||
                    (lineBytes_ == 1 && previous_ == '\r' && c != '\n');
                if (++bytes_ > 64 * 1024 || (startsField && ++fields_ > 128)) {
                    overflow_ = true;
                    admissionRejected(AdmissionStore::Headers, bytes_ > 64 * 1024 ? 64 * 1024 : 128,
                        bytes_ > 64 * 1024 ? bytes_ : fields_, bytes_ > 64 * 1024 ? "bytes" : "headers");
                    return -1;
                }
            }
            if (phase_ == Phase::ChunkSize) observeChunkSize(c);
            ++lineBytes_;
            if (previous_ == '\r' && c == '\n') {
                if (phase_ == Phase::RequestLine) phase_ = Phase::Headers;
                else if ((phase_ == Phase::Headers || phase_ == Phase::Trailers) && lineBytes_ == 2) phase_ = Phase::Body;
                else if (phase_ == Phase::ChunkEnd) { phase_ = Phase::ChunkSize; resetChunkSize(); }
                else if (phase_ == Phase::ChunkSize) {
                    remaining_ = negative_ ? 0UL - chunkSize_ : chunkSize_;
                    phase_ = remaining_ ? Phase::ChunkData : Phase::Trailers;
                    bytes_ = fields_ = 0;
                }
                lineBytes_ = 0;
            }
            previous_ = c;
        }
        return n;
    }
    ssize_t write(const char* data, size_t size) override {
        if (overflow_) {
            if (!overflowWritten_) {
                overflowWritten_ = true;
                const char* reply = requestLineOverflow_
                    ? "HTTP/1.1 414 URI Too Long\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"
                    : "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                if (!httplib::detail::write_data(stream_, reply, std::strlen(reply))) return -1;
            }
            return static_cast<ssize_t>(size);
        }
        // Bound (framing): upstream's keep-alive loop ignores response refusal/close headers; stop after every refusal.
        if (size >= 12 && std::memcmp(data, "HTTP/1.", 7) == 0 && data[9] >= '4' && data[9] <= '5') refused_ = true;
        return stream_.write(data, size);
    }
private:
    // Mirror only strtoul's hexadecimal prefix/value so body bytes never count as trailer fields.
    // Upstream remains responsible for rejecting malformed/overflowing chunk-size lines.
    void observeChunkSize(unsigned char c) {
        if (numberDone_) return;
        if (!numberStarted_) {
            if (std::isspace(c)) return;
            numberStarted_ = true;
            if (c == '+' || c == '-') { negative_ = c == '-'; return; }
        }
        if (digits_ == 1 && chunkSize_ == 0 && (c == 'x' || c == 'X') && !hexPrefix_) {
            hexPrefix_ = true; digits_ = 0; return;
        }
        const int digit = c >= '0' && c <= '9' ? c - '0' :
            c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) { numberDone_ = true; return; }
        if (chunkSize_ > (ULONG_MAX - static_cast<unsigned long>(digit)) / 16) chunkSize_ = ULONG_MAX;
        else chunkSize_ = chunkSize_ * 16 + static_cast<unsigned long>(digit);
        if (digits_ < 2) ++digits_;
    }
    void resetChunkSize() {
        chunkSize_ = 0; digits_ = 0;
        numberStarted_ = numberDone_ = negative_ = hexPrefix_ = false;
    }
    enum class Phase { RequestLine, Headers, Body, ChunkSize, ChunkData, ChunkEnd, Trailers };
    httplib::Stream& stream_;
    Phase phase_ = Phase::RequestLine;
    size_t bytes_ = 0, fields_ = 0, lineBytes_ = 0;
    unsigned long remaining_ = 0, chunkSize_ = 0;
    unsigned char previous_ = 0;
    int digits_ = 0;
    bool numberStarted_ = false, numberDone_ = false, negative_ = false, hexPrefix_ = false;
    bool overflow_ = false, overflowWritten_ = false, refused_ = false, requestLineOverflow_ = false;
};

static thread_local const McpIngressStream* activeIngressStream = nullptr;
// Socket exception boundary with observation of header-section and chunk metadata lines.
class McpSocketServer final : public httplib::Server {
    bool process_and_close_socket(socket_t sock) override {
        bool result = false;
        // Upstream catches routing exceptions only; parsing and response writing can also throw on a worker.
        try {
            std::string remote, local;
            int remotePort = 0, localPort = 0;
            httplib::detail::get_remote_ip_and_port(sock, remote, remotePort);
            httplib::detail::get_local_ip_and_port(sock, local, localPort);
            result = httplib::detail::process_server_socket(
                svr_sock_, sock, keep_alive_max_count_, keep_alive_timeout_sec_,
                read_timeout_sec_, read_timeout_usec_, write_timeout_sec_, write_timeout_usec_,
                [&](httplib::Stream& stream, bool close, bool& closed) {
                    McpIngressStream ingress(stream);
                    struct IngressScope {
                        const McpIngressStream* previous = activeIngressStream;
                        ~IngressScope() { activeIngressStream = previous; }
                    } ingressScope;
                    activeIngressStream = &ingress;
                    const bool ok = process_request(ingress, remote, remotePort, local, localPort,
                        close, closed, [&](httplib::Request& req) { ingress.body(req); });
                    if (ingress.overflowed()) ingress.write("", 0);
                    closed = closed || ingress.refused();
                    return ok;
                });
        } catch (...) {
            // No allocation/logging here: even a failed error response must retire the socket safely.
        }
        httplib::detail::shutdown_socket(sock);
        httplib::detail::close_socket(sock);
        return result;
    }
};

// HTTP ownership survives static destruction: std::exit may start on any
// thread while the listener and its request workers are still using it.
// A request worker cannot join the listener, which joins that worker in turn.
static thread_local bool handlingHttpRequest = false;
// The queue hook runs on the accept thread, before httplib reads any bytes.
class AdmittedQueue final : public httplib::TaskQueue {
public:
    explicit AdmittedQueue(const AdmissionLimits& limits) : limits_(limits), pool_(limits.workers) {}
    bool enqueue(std::function<void()> fn) override {
        auto& count = admissionState().connections;
        auto active = count.load();
        do {
            // Bound (security): queued, reading and deferred connections share admission. Overflow closes the new socket and is counted.
            if (active >= limits_.outstanding) {
                admissionRejected(AdmissionStore::Connections, limits_.outstanding, active + 1);
                return false;
            }
        } while (!count.compare_exchange_weak(active, active + 1));
        struct Credit {
            ~Credit() { --admissionState().connections; }
        };
        std::shared_ptr<Credit> credit;
        try {
            credit = std::make_shared<Credit>();
            return pool_.enqueue([fn = std::move(fn), credit] {
                struct WorkerScope {
                    bool previous = handlingHttpRequest;
                    WorkerScope() { handlingHttpRequest = true; }
                    ~WorkerScope() { handlingHttpRequest = previous; }
                } workerScope;
                fn();
            });
        } catch (...) {
            if (!credit) --count;
            return false;
        }
    }
    void shutdown() override { pool_.shutdown(); }
private:
    AdmissionLimits limits_;
    httplib::ThreadPool pool_;
};

struct ActiveHttpPromise {
    std::weak_ptr<std::promise<ReplyThunk>> promise;
    std::string body;
    std::shared_future<ReplyThunk> future;
};
std::vector<ActiveHttpPromise>& activeHttpPromises() {
    static auto* promises = new std::vector<ActiveHttpPromise>;
    return *promises;
}
void trackHttpPromise(const std::shared_ptr<std::promise<ReplyThunk>>& promise, const std::string& body,
                      std::shared_future<ReplyThunk> future) {
    activeHttpPromises().push_back({promise, body, std::move(future)});
}
void untrackHttpPromise(const std::shared_ptr<std::promise<ReplyThunk>>& promise) {
    std::lock_guard<std::mutex> lock(httpQueueMutex());
    auto& promises = activeHttpPromises();
    promises.erase(std::remove_if(promises.begin(), promises.end(),
        [&](const auto& entry) { return entry.promise.lock() == promise; }), promises.end());
}


std::mutex& httpQueueMutex() {
    static auto* mutex = new std::mutex;
    return *mutex;
}

static std::unique_ptr<ThreadChannel<McpRequest>>& httpChannelStorage() {
    static auto* channel = new std::unique_ptr<ThreadChannel<McpRequest>>(
        std::make_unique<ThreadChannel<McpRequest>>());
    return *channel;
}

ThreadChannel<McpRequest>& getHttpChannel() {
    return *httpChannelStorage();
}

void resetHttpChannel() {
    // A Public downgrade can stop the listener; a later grant starts it again.
    // Called before starting workers, after any previous workers were joined.
    std::lock_guard<std::mutex> lock(httpQueueMutex());
    httpChannelStorage() = std::make_unique<ThreadChannel<McpRequest>>();
}

std::unique_ptr<httplib::Server>& getHttpServer() {
    static auto* svr = new std::unique_ptr<httplib::Server>;
    return *svr;
}

std::unique_ptr<std::thread>& getHttpThread() {
    static auto* t = new std::unique_ptr<std::thread>;
    // Code grants also work in console apps without runApp's cleanup callback.
    // Register after all shutdown state, so it remains alive for this hook.
    static const bool cleanup = [] {
        (void)getHttpServer();
        (void)getHttpPort();
        (void)httpQueueMutex();
        (void)getHttpChannel();
        (void)deferredResponses();
        (void)deferredResponsesMutex();
        // An exception leaving an atexit handler terminates the process
        // (Windows) or unwinds into the code that called std::exit (glibc).
        std::atexit([] {
            try {
                if (getHttpServer()) stopHttpServer();
            } catch (...) {
            }
        });
        return true;
    }();
    (void)cleanup;
    return *t;
}

std::atomic<int>& getHttpPort() {
    static std::atomic<int> port{0};
    return port;
}

HttpListenerConfig& httpListenerConfig() {
    static HttpListenerConfig config;
    return config;
}

std::string& mcpAuthToken() {
    static std::string token;
    return token;
}

std::atomic<bool>& mcpLoopbackOnly() {
    static std::atomic<bool> loopback{true};
    return loopback;
}

void rejectRequest(httplib::Response& res, int status, const std::string& why) {
    res.status = status;
    res.set_content(dumpReply(json{{"error", why}}), "application/json");
}

// Browser-facing checks every request passes before anything else runs (#238,
// MCP Streamable HTTP transport: servers must validate Origin). A web page
// open in the user's browser can SEND requests to a loopback server even
// without CORS; these keep it out:
// - Host: when bound to loopback, it must name a loopback host. A DNS
//   rebinding page reaches 127.0.0.1 under its own domain name.
// - Origin: native MCP clients send none. When present, it must be the
//   server's own origin (http://localhost:PORT and the like).
// - Content-Type (POST): application/json only. Anything else is a request a
//   browser could send without a CORS preflight.
// Returns false (response filled: 403 / 415) when the request is refused.
bool checkRequest(const httplib::Request& req, httplib::Response& res, bool requireJson) {
    if (mcpLoopbackOnly().load() && req.has_header("Host") &&
        !isLoopbackHostHeader(req.get_header_value("Host"))) {
        rejectRequest(res, 403, "forbidden host '" + req.get_header_value("Host") +
                                "': the MCP server only answers to localhost, 127.0.0.1 or [::1]");
        return false;
    }
    if (req.has_header("Origin") &&
        !isAllowedOrigin(req.get_header_value("Origin"), getHttpPort().load())) {
        rejectRequest(res, 403, "forbidden origin '" + req.get_header_value("Origin") +
                                "': the MCP server is for native MCP clients, not web pages");
        return false;
    }
    if (requireJson && !isJsonContentType(req.get_header_value("Content-Type"))) {
        rejectRequest(res, 415, "unsupported Content-Type '" + req.get_header_value("Content-Type") +
                                "': send application/json");
        return false;
    }
    return true;
}


void startHttpServerImpl(int port, const std::string& host,
                         const std::string& token, bool healthEndpoint,
                         const char* opener) {
    if (environmentClosesMcp()) {
        if (getHttpServer()) stopHttpServer();
        return;
    }
    // An explicit start opens Public app tools even on an existing health listener.
    if (!healthEndpoint) healthOnlyEndpoint() = false;
    auto& svr = internal::getHttpServer();
    if (svr) return; // Already running

    const bool isLoopback = (host == "localhost" || host == "127.0.0.1" || host == "::1");
    if (!isLoopback && token.empty()) {
        trussc::logError("MCP")
            << "Refusing to bind MCP server to non-local host '" << host
            << "' without a token. Pass a token to startHttpServer to expose it (the MCP "
               "surface can inject input and mutate the scene).";
        return;
    }
    internal::mcpAuthToken() = token;
    internal::mcpLoopbackOnly().store(isLoopback);

    const auto limits = getAdmissionLimits();
    svr = std::make_unique<McpSocketServer>();
    svr->new_task_queue = [limits] { return new AdmittedQueue(limits); };
    // Bound (security): upstream limits Content-Length; the content receiver checks chunked and decoded body bytes.
    svr->set_payload_max_length(limits.bodyBytes);
    // Bound (security): inactive reads/writes and idle keep-alive sockets expire; a connection serves at most 100 requests.
    svr->set_read_timeout(5);
    svr->set_write_timeout(5);
    svr->set_keep_alive_timeout(5);
    svr->set_keep_alive_max_count(100);
    svr->set_idle_interval(0, 10000);
    svr->set_pre_routing_handler([limits](const httplib::Request& req, httplib::Response& res) {
        if (req.has_header("Content-Length") && req.get_header_value_u64("Content-Length") > limits.bodyBytes) {
            res.status = 413;
        } else if (req.method == "POST" && req.path == "/mcp") {
            if (!checkRequest(req, res, true)) {
                res.set_header("Connection", "close");
                return httplib::Server::HandlerResponse::Handled;
            }
        } else if ((req.has_header("Content-Length") || req.has_header("Transfer-Encoding"))) {
            // Only the MCP content receiver accepts bodies, including decoded data.
            res.status = req.method == "PRI" ? 400 : 404;
        }
        if (res.status >= 400) {
            res.set_header("Connection", "close");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
    svr->set_logger([limits](const httplib::Request&, const httplib::Response& res) {
        if (res.status == 413) admissionRejected(AdmissionStore::Body, limits.bodyBytes, limits.bodyBytes + 1);
        if (res.status == 414) admissionRejected(AdmissionStore::RequestLine, 8192, 8193);
    });
    svr->set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
        res.status = 500;
        res.set_header("Connection", "close");
        res.set_content(allocationErrorReply(), "application/json");
    });
    svr->set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 413) res.set_content("{\"error\":\"MCP body limit exceeded\"}", "application/json");
    });

    // A fixed port that is already in use fails to bind and is reported below.
    // POSIX: SO_REUSEADDR only, as TcpServer does. Windows: SO_EXCLUSIVEADDRUSE
    // only, so a port another socket holds fails to bind.
    svr->set_socket_options([](socket_t sock) {
#ifdef _WIN32
        BOOL opt = TRUE;
        setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    });

    // POST /mcp — JSON-RPC requests. No CORS header: MCP clients are native and
    // ignore CORS, while a wildcard origin would let any web page in the user's
    // browser drive the local server. (OPTIONS preflight handler dropped too.)
    svr->Post("/mcp", [limits](const httplib::Request& request, httplib::Response& res, const httplib::ContentReader& reader) {
        httplib::Request req = request;
        bool bodyRejected = false;
        const bool read = reader([&](const char* data, size_t count) {
            // Bound (security): decoded JSON is checked before append, independently of its wire representation.
            if (count > limits.bodyBytes - req.body.size()) {
                bodyRejected = true;
                return false;
            }
            req.body.append(data, count);
            return true;
        });
        const bool metadataRejected = activeIngressStream && activeIngressStream->overflowed();
        if (!read || metadataRejected) {
            if (metadataRejected) res.status = 431;
            else if (bodyRejected) res.status = 413;
            res.set_header("Connection", "close");
            return;
        }
        struct RequestScope {
            bool previous = handlingHttpRequest;
            RequestScope() { handlingHttpRequest = true; }
            ~RequestScope() { handlingHttpRequest = previous; }
        } requestScope;
        if (!internal::checkRequest(req, res, true)) return;

        // Bearer auth when a token is configured (always so for non-loopback).
        const std::string& tok = internal::mcpAuthToken();
        if (!tok.empty()) {
            auto it = req.headers.find("Authorization");
            if (it == req.headers.end() || !internal::bearerTokenMatches(it->second, tok)) {
                res.status = 401;
                res.set_content("{\"error\":\"unauthorized\"}", "application/json");
                return;
            }
        }

        auto p = std::make_shared<std::promise<internal::ReplyThunk>>();
        auto f = p->get_future().share();
        struct PromiseScope {
            std::shared_ptr<std::promise<ReplyThunk>> promise;
            ~PromiseScope() { untrackHttpPromise(promise); }
        } promiseScope{p};

        McpRequest mcpReq;
        mcpReq.body = req.body;
        mcpReq.response = p;

        internal::enqueueHttpRequest(std::move(mcpReq), f);

        // Block until main thread processes the request, then execute the
        // reply thunk HERE — heavy two-stage work (thumbnail encode etc.) runs
        // on this worker thread, not the frame loop.
        const internal::ReplyThunk& thunk = f.get();
        std::string result;
        try {
            result = thunk();
        } catch (const AdmissionError&) {
            res.status = 503;
            res.set_header("X-MCP-Reason", "MCP admission limit exceeded");
            return;
        } catch (const std::bad_alloc&) {
            const auto parsed = parseRequest(req.body);
            result = allocationErrorReply(parsed.is_object() && parsed.contains("id") ? parsed["id"] : json(nullptr));
        } catch (const std::exception& e) {
            result = "{\"error\":\"MCP reply construction failed\"}";
        }
        res.set_content(result, "application/json");
    });

    // GET / — Server info
    svr->Get("/", [](const httplib::Request& req, httplib::Response& res) {
        if (!internal::checkRequest(req, res, false)) return;
        json info = {
            {"name", "TrussC MCP Server"},
            {"transport", "http"},
            {"endpoint", "/mcp"}
        };
        res.set_content(internal::dumpReply(info), "application/json");
    });

    int actualPort = port > 0 ? (svr->bind_to_port(host.c_str(), port) ? port : -1)
                              : svr->bind_to_any_port(host.c_str());
    if (actualPort <= 0) {
        trussc::logError("MCP") << "Failed to bind HTTP server to " << host << ":" << port;
        svr.reset();
        return;
    }
    healthOnlyEndpoint() = healthEndpoint;
    internal::resetHttpChannel();
    internal::getHttpPort().store(actualPort);
    httpListenerConfig() = {port, host, opener};
    internal::getHttpThread() = std::make_unique<std::thread>([]() {
        internal::getHttpServer()->listen_after_bind();
    });
    svr->wait_until_ready();
    // Binding and readiness complete on the caller's thread. Deliver the
    // Notice here too, so Logger listeners do not run on the HTTP thread.
    trussc::logNotice("MCP") << "HTTP server listening on http://" << host
                             << ":" << actualPort << "/mcp";
}

#endif // __EMSCRIPTEN__

Server& Server::instance() {
    static Server server;
    // Build the core registry once, even when the first registration is from
    // a guest. Core handlers outlive that guest and must have no guest owner.
    static const bool registered = [&] {
        const auto owner = internal::registrationOwner();
        internal::setRegistrationOwner(nullptr);
        internal::registerStandardTools(server);
        internal::setRegistrationOwner(owner);
        return true;
    }();
    (void)registered;
    return server;
}

} // namespace internal

#ifndef __EMSCRIPTEN__
void startHttpServer(int port, const std::string& host, const std::string& token) {
    // Reject before changing exposure or explicit ownership. A rejected call
    // must not widen a health-only listener or preserve one on Public downgrade.
    if (internal::getHttpServer() && !internal::environmentClosesMcp()) {
        const auto& config = internal::httpListenerConfig();
        if ((port != config.requestedPort && port != getHttpPort()) ||
            host != config.host || token != internal::mcpAuthToken()) {
            logError("MCP") << "already listening on " << config.host << ":" << getHttpPort()
                << ", opened by " << config.opener
                << "; call startHttpServer(port, host, token) before setToolLevel() to choose the address";
            return;
        }
    }
    const bool wasFull = internal::getHttpServer() && !internal::healthOnly();
    internal::startHttpServerImpl(port, host, token, false, "startHttpServer()");
    if (internal::getHttpServer()) {
        internal::explicitlyOpened() = true;
        if (!wasFull) internal::logExposure();
    }
}

void stopHttpServer() {
    {
        std::vector<McpRequest> queued;
        {
            std::lock_guard<std::mutex> lock(internal::httpQueueMutex());
            queued = internal::getHttpChannel().receiveAll();
            internal::getHttpChannel().close();
        }
        for (auto& request : queued) {
            internal::completeTerminalResponse(request.response, [&] {
                return internal::normalExitReply(request.body);
            });
        }
    }
    // Unblock any HTTP workers still waiting on a deferred reply — no more frames
    // will be presented, so their producers would never run (and a blocked
    // worker would stall the server shutdown below).
    {
        // Take them out under the lock: on an HTTP worker's std::exit the main
        // thread may be draining the same list (see deferredResponsesMutex()).
        std::vector<internal::DeferredResponse> list;
        {
            std::lock_guard<std::mutex> lock(internal::deferredResponsesMutex());
            list.swap(internal::deferredResponses());
        }
        for (auto& d : list) {
            // During an app exit, say so (with the reason); otherwise the
            // server was just stopped.
            internal::completeTerminalResponse(d.response, [&] {
                const std::string message = trussc::internal::exitReason().empty()
                    ? std::string("the MCP server shut down before the reply was produced")
                    : internal::normalExitMessage();
                return internal::toolError(d.id, message);
            }, d.id);
        }
        list.clear();
    }

    // A main-thread handler may have removed its request from the channel
    // without publishing a deferral yet. Complete those promises as well.
    {
        std::remove_reference_t<decltype(internal::activeHttpPromises())> active;
        {
            std::lock_guard<std::mutex> lock(internal::httpQueueMutex());
            active.swap(internal::activeHttpPromises());
        }
        for (auto& entry : active) {
            // Queued/deferred completion may already have answered this entry.
            // Avoid rebuilding an error (and charging it) for a ready promise.
            if (entry.future.valid() && entry.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                continue;
            if (auto promise = entry.promise.lock()) {
                internal::completeTerminalResponse(promise, [&] {
                    return internal::normalExitReply(entry.body);
                });
            }
        }
    }

    auto& svr = internal::getHttpServer();
    if (svr) {
        svr->stop();
    }

    internal::getHttpChannel().close();

    auto& t = internal::getHttpThread();
    const bool onHttpThread = t && t->joinable() &&
        (t->get_id() == std::this_thread::get_id() || internal::handlingHttpRequest);
    if (t && t->joinable()) {
        if (onHttpThread) t->detach();
        else t->join();
    }
    t.reset();
    // A detached listener/handler still needs its server until process exit.
    if (onHttpThread) (void)svr.release();
    else svr.reset();
    internal::getHttpPort().store(0);
    internal::explicitlyOpened() = false;
    internal::healthOnlyEndpoint() = false;
}

#endif

void setToolLevel(ToolLevel level) {
    const auto previousLevel = getToolLevel();
#ifndef __EMSCRIPTEN__
    const bool wasFull = internal::getHttpServer() && !internal::healthOnly();
#endif
    // Dev builds stay Control, so the Public downgrade policy never applies.
    internal::grantedToolLevel().store(internal::isDevBuild() ? ToolLevel::Control : level);
    if (internal::environmentClosesMcp()) {
        internal::startConfiguredServer("the grant");
    } else if (getToolLevel() >= ToolLevel::ReadOnly) {
        internal::healthOnlyEndpoint() = false;
        internal::startConfiguredServer("the grant");
    } else if (previousLevel > ToolLevel::Public && !internal::explicitlyOpened()) {
#ifndef __EMSCRIPTEN__
        if (internal::getHttpServer()) {
            if (internal::environmentRequestsMcp()) internal::healthOnlyEndpoint() = true;
            else stopHttpServer();
        }
#endif
    }
#ifndef __EMSCRIPTEN__
    if (!wasFull || getToolLevel() > previousLevel) internal::logExposure();
#endif
}

ToolLevel getToolLevel() {
    return internal::grantedToolLevel().load();
}

} // namespace mcp
} // namespace trussc
