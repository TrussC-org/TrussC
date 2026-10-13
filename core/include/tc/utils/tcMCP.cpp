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
#include "tcMcpBuild.h"

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
    static std::vector<DeferredResponse> v;
    return v;
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

std::mutex& httpQueueMutex() {
    static std::mutex mutex;
    return mutex;
}

static std::unique_ptr<ThreadChannel<McpRequest>>& httpChannelStorage() {
    static auto channel = std::make_unique<ThreadChannel<McpRequest>>();
    return channel;
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
    static std::unique_ptr<httplib::Server> svr;
    return svr;
}

std::unique_ptr<std::thread>& getHttpThread() {
    static std::unique_ptr<std::thread> t;
    // Code grants also work in console apps without runApp's cleanup callback.
    // Register after all shutdown state, so it remains alive for this hook.
    static const bool cleanup = [] {
        (void)getHttpServer();
        (void)getHttpPort();
        (void)httpQueueMutex();
        (void)getHttpChannel();
        (void)deferredResponses();
        std::atexit([] { if (getHttpServer()) stopHttpServer(); });
        return true;
    }();
    (void)cleanup;
    return t;
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
