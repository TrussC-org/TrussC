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

namespace internal {

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

// HTTP ownership survives static destruction: std::exit may start on any
// thread while the listener and its request workers are still using it.
// A request worker cannot join the listener, which joins that worker in turn.
static thread_local bool handlingHttpRequest = false;

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

    svr = std::make_unique<httplib::Server>();

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
    svr->Post("/mcp", [](const httplib::Request& req, httplib::Response& res) {
        struct RequestScope {
            RequestScope() { handlingHttpRequest = true; }
            ~RequestScope() { handlingHttpRequest = false; }
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
        auto f = p->get_future();

        McpRequest mcpReq;
        mcpReq.body = req.body;
        mcpReq.response = p;

        internal::enqueueHttpRequest(std::move(mcpReq));

        // Block until main thread processes the request, then execute the
        // reply thunk HERE — heavy two-stage work (thumbnail encode etc.) runs
        // on this worker thread, not the frame loop.
        internal::ReplyThunk thunk = f.get();
        std::string result;
        try {
            result = thunk();
        } catch (const std::exception& e) {
            result = internal::dumpReply(json{{"error", std::string("reply construction failed: ") + e.what()}});
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
        std::lock_guard<std::mutex> lock(internal::httpQueueMutex());
        auto queued = internal::getHttpChannel().receiveAll();
        internal::getHttpChannel().close();
        for (auto& request : queued) {
            const auto reply = internal::normalExitReply(request.body);
            request.response->set_value([reply] { return reply; });
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
            const std::string message = trussc::internal::exitReason().empty()
                ? std::string("the MCP server shut down before the reply was produced")
                : internal::normalExitMessage();
            std::string reply = internal::toolError(d.id, message);
            d.response->set_value([reply]() { return reply; });
        }
        list.clear();
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
