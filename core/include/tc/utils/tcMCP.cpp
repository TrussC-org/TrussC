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

namespace detail {

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

bool healthOnly() {
    return healthOnlyEndpoint() && getToolLevel() == ToolLevel::Public;
}

bool environmentClosesMcp() {
    const char* value = std::getenv("TRUSSC_MCP");
    return value && std::string_view(value) == "0";
}

const std::string& mainWindowTitle(const std::string& title) {
    // buildAppDescriptor returns before sapp_run consumes the title pointer.
    // Keep its backing string in the host, including on hot reload builds.
    static std::string windowTitle;
    windowTitle = isDevBuild() && title.find("[MCP]") == std::string::npos
        ? title + " [MCP]" : title;
    return windowTitle;
}

void startConfiguredServer() {
#ifndef __EMSCRIPTEN__
    if (environmentClosesMcp()) {
        if (getHttpServer()) stopHttpServer();
        return;
    }
    const char* envMcp = std::getenv("TRUSSC_MCP");
    const char* envPort = std::getenv("TRUSSC_MCP_PORT");
    const bool healthEndpoint = getToolLevel() == ToolLevel::Public;
    if (!isDevBuild() && healthEndpoint &&
        !(envMcp && std::string_view(envMcp) == "1") && !envPort) return;
    if (getHttpServer()) return;
    const char* host = isDevBuild() ? std::getenv("TRUSSC_MCP_HOST") : nullptr;
    const char* token = isDevBuild() ? std::getenv("TRUSSC_MCP_TOKEN") : nullptr;
    startHttpServerImpl(envPort ? std::atoi(envPort) : 0,
                        host ? host : "127.0.0.1", token ? token : "", healthEndpoint);
    if (!getHttpServer()) return;
    if (healthEndpoint) {
        logNotice("MCP") << "MCP: health only (ToolLevel Public). The supervisor gets logs, restart and hang detection; for screenshots and status add mcp::setToolLevel(mcp::ToolLevel::ReadOnly) in setup().";
    }
#endif
}

void initializeMcp() {
#ifndef __EMSCRIPTEN__
    // App::onSetupDone also runs for replacement guests; startup is host state.
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    if (isDevBuild())
        logWarning("MCP") << "MCP development build: Control tools are enabled. Do not distribute this build.";
    startConfiguredServer();
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

ThreadChannel<McpRequest>& getHttpChannel() {
    static ThreadChannel<McpRequest> channel;
    return channel;
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

std::string& mcpAuthToken() {
    static std::string token;
    return token;
}

std::atomic<bool>& mcpLoopbackOnly() {
    static std::atomic<bool> loopback{true};
    return loopback;
}

#endif // __EMSCRIPTEN__

} // namespace detail

Server& Server::instance() {
    static Server server;
    // Build the core registry once, even when the first registration is from
    // a guest. Core handlers outlive that guest and must have no guest owner.
    static const bool registered = [&] {
        const auto owner = detail::registrationOwner();
        detail::setRegistrationOwner(nullptr);
        detail::registerStandardTools(server);
        detail::setRegistrationOwner(owner);
        return true;
    }();
    (void)registered;
    return server;
}

void setToolLevel(ToolLevel level) {
    detail::grantedToolLevel().store(detail::isDevBuild() ? ToolLevel::Control : level);
    if (getToolLevel() >= ToolLevel::ReadOnly) {
        detail::healthOnlyEndpoint() = false;
        detail::startConfiguredServer();
    } else if (detail::environmentClosesMcp()) {
        detail::startConfiguredServer();
    }
}

ToolLevel getToolLevel() {
    return detail::grantedToolLevel().load();
}

} // namespace mcp
} // namespace trussc
