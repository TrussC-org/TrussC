#pragma once

// =============================================================================
// tcMCP.h - Model Context Protocol (MCP) Server Implementation
// =============================================================================
// HTTP transport via cpp-httplib. Requests arrive on a worker thread and are
// forwarded to the main (GL) thread through ThreadChannel + promise so that
// tool handlers can safely access graphics state.
// =============================================================================

#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <sstream>
#include <future>
#include <thread>
#include <atomic>
#include <mutex>
#include <cctype>
#include <chrono>

// JSON support
#include "../../nlohmann/json.hpp"
using json = nlohmann::json;

#include "tcLog.h"
#include "tcThreadChannel.h"
#include "tcVersion.h"

#ifndef __EMSCRIPTEN__
#include "../../impl/httplib.h"
#endif

namespace trussc {
namespace mcp {

// ---------------------------------------------------------------------------
// Deferred tool responses
// ---------------------------------------------------------------------------
// Some tools (e.g. tc_get_screenshot) need state that only exists AFTER present()
// — during the frame, drawing is still deferred and a readback would be blank.
// Such a handler calls deferToolResultUntilAfterFrame(): processHttpQueue()
// stashes the request instead of answering it, and drainDeferredResponses()
// (invoked from an afterFrame listener) runs the producer at the safe readback
// point and unblocks the waiting HTTP worker. All state below is touched only
// on the main thread inside processHttpQueue()/handleToolsCall().
//
// A deferral can name a TARGET window (an opaque WindowContext*): its producer
// then runs right after THAT window's present(), inside its own tick, where
// its drawable is the current one (a secondary window's readback is only valid
// there — #243). The main window drains untargeted entries from its afterFrame
// listener; each secondary window drains its own from its tick.

namespace detail {

// The promise carries a THUNK, not the reply string: the blocked HTTP worker
// executes it (future.get()()) to obtain the reply. For ordinary tools the
// thunk just returns a string built on the main thread; two-stage tools (see
// deferToolResultTwoStage) put their heavy encode work inside the thunk so it
// runs on the HTTP worker — off the frame loop — while main only did the
// readback.
using ReplyThunk = std::function<std::string()>;

struct DeferredResponse {
    std::shared_ptr<std::promise<ReplyThunk>> response;  // unblocks the HTTP worker
    std::function<ReplyThunk()> makeEnvelope;            // main stage → worker thunk
    const void* target = nullptr;                        // window to run in (null = main)
    std::chrono::steady_clock::time_point deadline;      // targeted: give up after this
    std::function<std::string()> timeoutReply;           // targeted: reply when given up
    // Registration owner of the code the producer runs (DeferralState::owner):
    // a hot reload guest generation, or null for host code. When that owner
    // is removed, removeRegistrationsOwnedBy() answers the entry with
    // errorReply instead of running its producer, which may reach the App
    // about to be deleted (a guest tool capturing `this`, a status-image
    // getter). A host tool's deferral is not affected.
    const void* owner = nullptr;
    std::function<std::string(const std::string&)> errorReply;  // tool error with this message
};

// A targeted deferral whose window renders no frame in this time (minimized,
// hidden, throttled very low, closed) is answered with an error instead of
// leaving the HTTP worker blocked.
inline constexpr std::chrono::seconds kTargetedDeferralTimeout{5};

struct DeferralState {
    bool requested = false;                  // set by deferToolResultUntilAfterFrame()
    std::function<json()> produce;           // tool content producer (runs fully on main)
    bool twoStageRequested = false;          // set by deferToolResultTwoStage()
    std::function<std::function<json()>()> produceTwoStage;  // main stage → worker stage
    const void* target = nullptr;            // window the deferral runs in (null = main)
    bool hasEnvelope = false;                // set by handleToolsCall(), read by processHttpQueue()
    std::function<ReplyThunk()> envelope;    // JSON-RPC reply builder (main part)
    std::function<std::string()> timeoutReply;  // targeted deferral given up (see above)
    std::function<std::string(const std::string&)> errorReply;  // deferral cancelled (unload)
    // Registration owner of the code the deferred producer runs: the called
    // tool's owner, set by handleToolsCall(); a host tool that runs code
    // someone else registered (tc_get_status_image a status-image getter)
    // names that code's owner with setDeferralOwner().
    const void* owner = nullptr;
};
// The MCP state below is one per process, so it is defined non-inline in
// tcMCP.cpp: a hot reload guest on Windows would otherwise get its own copy of
// each, and a tool handler it runs would defer into a DeferralState the host
// never reads (#249; docs/ARCHITECTURE.md, "One instance per process").
DeferralState& deferralState();

// Call after deferring, in a tool handler whose deferred producer runs code
// registered under another owner (see DeferralState::owner).
inline void setDeferralOwner(const void* owner) { deferralState().owner = owner; }

std::vector<DeferredResponse>& deferredResponses();

// Set by registerControlTools() (which is web-available), so this flag must
// live outside the server-only #ifndef block below.
std::atomic<bool>& isDebuggerEnabled();

} // namespace detail

// Check if debugger tools are registered (input injection / scene mutation).
// The flag is set by registerControlTools() — registering IS the opt-in, so
// apps/addons can query this to tell whether the debugger surface is exposed.
inline bool isDebuggerEnabled() {
    return detail::isDebuggerEnabled().load();
}

// Call inside a tool handler to defer its result until just after the next
// present(). `produce` returns the tool's content json (same shape a normal
// handler would return) and runs at that safe readback point. `targetWindow`
// (a WindowContext*, null = the main window) picks whose present() — the
// producer then runs inside that window's tick, with its context current.
inline void deferToolResultUntilAfterFrame(std::function<json()> produce,
                                           const void* targetWindow = nullptr) {
    detail::deferralState().requested = true;
    detail::deferralState().produce = std::move(produce);
    detail::deferralState().target = targetWindow;
}

// Two-stage variant for tools whose result is expensive to build (e.g.
// tc_get_thumbnail): `mainStage` runs at the afterFrame safe point on the MAIN
// thread — do the GPU readback there and nothing else — and the closure it
// returns runs on the HTTP worker thread that is already sitting blocked on
// this reply. Put the heavy work (downscale, encode) in that closure and the
// frame loop never pays for it.
// `targetWindow`: as for deferToolResultUntilAfterFrame().
inline void deferToolResultTwoStage(std::function<std::function<json()>()> mainStage,
                                    const void* targetWindow = nullptr) {
    detail::deferralState().twoStageRequested = true;
    detail::deferralState().produceTwoStage = std::move(mainStage);
    detail::deferralState().target = targetWindow;
}

// Run the deferred main stages aimed at `targetWindow` (null = the main
// window) and unblock their HTTP workers. Call right after that window's
// present(): the main window from an events().afterFrame listener, a secondary
// window from its tick. Only the main part of each envelope runs here; the
// returned thunk executes on the HTTP worker. The main window's call also
// answers targeted entries that ran past their deadline.
inline void drainDeferredResponses(const void* targetWindow = nullptr) {
    auto& list = detail::deferredResponses();
    if (list.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    std::vector<detail::DeferredResponse> keep;
    for (auto& d : list) {
        if (d.target == targetWindow) {
            detail::ReplyThunk thunk;
            try {
                thunk = d.makeEnvelope();
            } catch (const std::exception& e) {
                std::string err = std::string("{\"error\":\"deferred response failed: ") + e.what() + "\"}";
                thunk = [err]() { return err; };
            }
            d.response->set_value(std::move(thunk));
        } else if (!targetWindow && d.target && now >= d.deadline) {
            std::string reply = d.timeoutReply ? d.timeoutReply()
                                               : std::string("{\"error\":\"window did not render\"}");
            d.response->set_value([reply]() { return reply; });
        } else {
            keep.push_back(std::move(d));
        }
    }
    list.swap(keep);
}

inline bool hasDeferredResponses() { return !detail::deferredResponses().empty(); }

// ---------------------------------------------------------------------------
// Types & Interfaces
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Registration owners (#227)
// ---------------------------------------------------------------------------
// While an owner is set, every tool / resource / status entry registered is
// tagged with it, and removeRegistrationsOwnedBy() takes them all out again.
// The hot reload host sets one per guest generation, so a reload drops what
// the old guest registered — handlers that capture the old App — before that
// App is deleted. Plain apps never set one (tag null = permanent).
// Both accessors are defined in tcMCP.cpp: the host sets the owner and a guest
// registers under it, so they must see the same one on every platform.
namespace detail {
const void*& registrationOwner();
// Registries defined elsewhere (the status registries of tcStandardTools.h,
// in tcMCP.cpp) hook their own cleanup in here the first time they are used.
std::vector<std::function<void(const void*)>>& ownerCleanupHooks();
inline void setRegistrationOwner(const void* owner) { registrationOwner() = owner; }
inline void removeRegistrationsOwnedBy(const void* owner);   // after Server
} // namespace detail

struct ToolArg {
    std::string name;
    std::string type; // "string", "int", "float", "boolean", "object", "array"
    std::string description;
    bool required = true;
};

class Tool {
public:
    std::string name;
    std::string description;
    std::vector<ToolArg> args;
    std::function<json(const json&)> handler;
    const void* owner = nullptr;   // see registrationOwner()

    json getSchema() const {
        json schema = {
            {"type", "object"},
            {"properties", json::object()},
            {"required", json::array()}
        };

        for (const auto& arg : args) {
            schema["properties"][arg.name] = {
                {"type", arg.type},
                {"description", arg.description}
            };
            if (arg.required) {
                schema["required"].push_back(arg.name);
            }
        }
        return schema;
    }
};

class Resource {
public:
    std::string uri;
    std::string name;
    std::string mimeType;
    std::string description;
    std::function<std::string()> handler; // Returns content (text or base64)
    const void* owner = nullptr;   // see registrationOwner()
};

// ---------------------------------------------------------------------------
// MCP Server Core
// ---------------------------------------------------------------------------

class Server {
public:
    // The one registry every tool / resource goes into. Defined in tcMCP.cpp,
    // so a hot reload guest registers into the host's server (#249).
    static Server& instance();

    // --- Registration API ---

    void registerTool(const Tool& tool) {
        // Last registration wins. Warn, because silently replacing a standard
        // tool (they register before setup()) is a hard bug to spot.
        if (tools_.count(tool.name)) {
            logWarning("MCP") << "tool '" << tool.name << "' re-registered; previous handler replaced";
        }
        tools_[tool.name] = tool;
        tools_[tool.name].owner = detail::registrationOwner();
    }

    void registerResource(const Resource& res) {
        resources_[res.uri] = res;
        resources_[res.uri].owner = detail::registrationOwner();
    }

    bool hasTool(const std::string& name) const { return tools_.count(name) != 0; }

    // Drop every tool / resource registered under `owner` (non-null).
    void removeOwnedBy(const void* owner) {
        if (!owner) return;
        for (auto it = tools_.begin(); it != tools_.end();) {
            it = (it->second.owner == owner) ? tools_.erase(it) : std::next(it);
        }
        for (auto it = resources_.begin(); it != resources_.end();) {
            it = (it->second.owner == owner) ? resources_.erase(it) : std::next(it);
        }
    }

    // --- Message Processing (returns JSON-RPC response string) ---

    std::string processMessage(const std::string& rawMessage) {
        try {
            auto j = json::parse(rawMessage);

            // Validate JSON-RPC
            if (!j.contains("jsonrpc") || j["jsonrpc"] != "2.0") {
                return makeError(json(nullptr), -32600, "Invalid JSON-RPC");
            }

            // Handle Request
            if (j.contains("method")) {
                return handleRequest(j);
            }
            // Handle Response (not implemented for server role)
            else if (j.contains("result") || j.contains("error")) {
                return ""; // Ignore
            }

            return "";

        } catch (const std::exception& e) {
            trussc::logError("MCP") << "JSON parse error: " << e.what();
            return makeError(json(nullptr), -32700, std::string("Parse error: ") + e.what());
        }
    }

private:
    std::map<std::string, Tool> tools_;
    std::map<std::string, Resource> resources_;

    std::string handleRequest(const json& req) {
        std::string method = req["method"];
        auto id = req.contains("id") ? req["id"] : json(nullptr);

        if (method == "initialize") {
            return handleInitialize(req, id);
        } else if (method == "tools/list") {
            return handleToolsList(req, id);
        } else if (method == "tools/call") {
            return handleToolsCall(req, id);
        } else if (method == "resources/list") {
            return handleResourcesList(req, id);
        } else if (method == "resources/read") {
            return handleResourcesRead(req, id);
        } else {
            if (!id.is_null()) {
                return makeError(id, -32601, "Method not found: " + method);
            }
            return "";
        }
    }

    std::string handleInitialize(const json& req, const json& id) {
        json result = {
            {"protocolVersion", "2024-11-05"},
            {"server", {
                {"name", "TrussC App"},
                {"version", getVersion()}
            }},
            {"capabilities", {
                {"tools", {}},
                {"resources", {}}
            }}
        };
        return makeResult(id, result);
    }

    std::string handleToolsList(const json& req, const json& id) {
        json toolList = json::array();
        for (const auto& [name, tool] : tools_) {
            toolList.push_back({
                {"name", tool.name},
                {"description", tool.description},
                {"inputSchema", tool.getSchema()}
            });
        }
        return makeResult(id, {{"tools", toolList}});
    }

    std::string handleToolsCall(const json& req, const json& id) {
        auto params = req["params"];
        std::string name = params["name"];
        auto args = params["arguments"];

        if (tools_.find(name) == tools_.end()) {
            return makeError(id, -32601, "Tool not found: " + name);
        }

        // Wrap a tool's content json into a full JSON-RPC result string.
        auto formatResult = [this, id](const json& content) -> std::string {
            json result;
            if (content.is_array() && content.size() > 0 && content[0].contains("type")) {
                result = {{"content", content}};
            } else {
                result = {{"content", {{
                    {"type", "text"},
                    {"text", content.dump()}
                }}}};
            }
            return makeResult(id, result);
        };

        try {
            auto& ds = detail::deferralState();
            ds.requested = false;
            ds.twoStageRequested = false;
            ds.owner = tools_[name].owner;   // the handler may name another (setDeferralOwner)

            // Execute tool handler (may call deferToolResultUntilAfterFrame()
            // or deferToolResultTwoStage())
            json content = tools_[name].handler(args);

            // Handler asked to produce its result after the next present().
            if (ds.requested || ds.twoStageRequested) {
                ds.errorReply = [formatResult](const std::string& message) -> std::string {
                    return formatResult(json{{"status", "error"}, {"message", message}});
                };
            }
            if (ds.target) {
                ds.timeoutReply = [formatResult]() -> std::string {
                    return formatResult(json{{"status", "error"},
                        {"message", "the window rendered no frame within 5 s (minimized, hidden or closed?)"}});
                };
            }

            if (ds.requested) {
                auto produce = std::move(ds.produce);
                ds.requested = false;
                ds.hasEnvelope = true;
                // Everything runs on main at drain time; the worker thunk just
                // hands back the prebuilt string.
                ds.envelope = [formatResult, produce]() -> detail::ReplyThunk {
                    std::string reply = formatResult(produce());
                    return [reply]() { return reply; };
                };
                return std::string();  // processHttpQueue() stashes the reply
            }

            // Two-stage: main stage at drain time (readback), returned closure
            // — wrapped so the JSON-RPC formatting ALSO happens on the worker —
            // executes on the blocked HTTP worker thread.
            if (ds.twoStageRequested) {
                auto mainStage = std::move(ds.produceTwoStage);
                ds.twoStageRequested = false;
                ds.hasEnvelope = true;
                ds.envelope = [formatResult, mainStage]() -> detail::ReplyThunk {
                    std::function<json()> workerStage = mainStage();
                    return [formatResult, workerStage]() -> std::string {
                        try {
                            return formatResult(workerStage());
                        } catch (const std::exception& e) {
                            return std::string("{\"error\":\"deferred worker stage failed: ") + e.what() + "\"}";
                        }
                    };
                };
                return std::string();  // processHttpQueue() stashes the reply
            }

            return formatResult(content);

        } catch (const std::exception& e) {
            return makeError(id, -32000, std::string("Tool execution error: ") + e.what());
        }
    }

    std::string handleResourcesList(const json& req, const json& id) {
        json resList = json::array();
        for (const auto& [uri, res] : resources_) {
            resList.push_back({
                {"uri", res.uri},
                {"name", res.name},
                {"description", res.description},
                {"mimeType", res.mimeType.empty() ? nullptr : json(res.mimeType)}
            });
        }
        return makeResult(id, {{"resources", resList}});
    }

    std::string handleResourcesRead(const json& req, const json& id) {
        std::string uri = req["params"]["uri"];

        if (resources_.find(uri) == resources_.end()) {
            return makeError(id, -32602, "Resource not found: " + uri);
        }

        try {
            std::string content = resources_[uri].handler();
            json resourceContent = {
                {"uri", uri},
                {"mimeType", resources_[uri].mimeType}
            };
            resourceContent["text"] = content;
            return makeResult(id, {{"contents", {resourceContent}}});

        } catch (const std::exception& e) {
            return makeError(id, -32000, std::string("Resource read error: ") + e.what());
        }
    }

    std::string makeResult(const json& id, const json& result) {
        if (id.is_null()) return "";
        json res = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", result}
        };
        return res.dump();
    }

    std::string makeError(const json& id, int code, const std::string& message) {
        if (id.is_null()) return "";
        json res = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {
                {"code", code},
                {"message", message}
            }}
        };
        return res.dump();
    }
};

// ---------------------------------------------------------------------------
// HTTP Server (main thread synchronization via ThreadChannel + promise)
// ---------------------------------------------------------------------------

#ifndef __EMSCRIPTEN__

struct McpRequest {
    std::string body;
    // Carries a thunk the HTTP worker executes to obtain the reply string —
    // see detail::ReplyThunk. Normal replies are prebuilt (thunk just returns
    // them); two-stage tool replies do their heavy encode inside the thunk.
    std::shared_ptr<std::promise<detail::ReplyThunk>> response;
};

namespace detail {

// HTTP server state, one per process (defined in tcMCP.cpp, see above).
ThreadChannel<McpRequest>& getHttpChannel();
std::unique_ptr<httplib::Server>& getHttpServer();
std::unique_ptr<std::thread>& getHttpThread();
std::atomic<int>& getHttpPort();

// Bearer token required on /mcp requests. Empty = no auth (localhost default).
std::string& mcpAuthToken();

// Whether the server is bound to a loopback address (the Host check applies).
std::atomic<bool>& mcpLoopbackOnly();

inline std::string asciiLower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline std::string trimSpaces(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

// "localhost:8080" / "127.0.0.1" / "[::1]:8080" -> is the host part loopback?
inline bool isLoopbackHostHeader(const std::string& hostHeader) {
    std::string h = asciiLower(trimSpaces(hostHeader));
    std::string name;
    if (!h.empty() && h[0] == '[') {                 // [v6]:port
        size_t close = h.find(']');
        if (close == std::string::npos) return false;
        name = h.substr(0, close + 1);
    } else {
        size_t colon = h.find(':');
        name = (colon == std::string::npos) ? h : h.substr(0, colon);
    }
    return name == "localhost" || name == "127.0.0.1" || name == "[::1]";
}

// Only the server's own origins. The server is for native MCP clients: it
// sends no CORS headers, so no web page can call it, neither directly nor
// through a dev-server proxy that forwards the page's Origin (#346).
inline bool isAllowedOrigin(const std::string& origin, int port) {
    std::string o = asciiLower(trimSpaces(origin));
    while (!o.empty() && o.back() == '/') o.pop_back();
    const std::string p = std::to_string(port);
    return o == "http://localhost:" + p || o == "http://127.0.0.1:" + p || o == "http://[::1]:" + p;
}

// "application/json", optionally with parameters ("; charset=utf-8").
inline bool isJsonContentType(const std::string& contentType) {
    std::string t = contentType.substr(0, contentType.find(';'));
    return asciiLower(trimSpaces(t)) == "application/json";
}

// Equality without an early exit: every byte of the longer input is visited
// and the differences are OR-ed together, so the time taken does not depend
// on where the inputs first differ. A length mismatch is folded into the
// result instead of returning early; the time still follows the longer
// length.
inline bool constantTimeEquals(std::string_view a, std::string_view b) {
    const size_t n = a.size() > b.size() ? a.size() : b.size();
    unsigned int diff = (a.size() == b.size()) ? 0u : 1u;
    for (size_t i = 0; i < n; ++i) {
        unsigned char x = i < a.size() ? (unsigned char)a[i] : 0;
        unsigned char y = i < b.size() ? (unsigned char)b[i] : 0;
        diff |= (unsigned int)(x ^ y);
    }
    return diff == 0;
}

// Authorization header value "Bearer <token>". The scheme is not secret and
// is checked first; the token part goes through constantTimeEquals().
inline bool bearerTokenMatches(std::string_view header, std::string_view token) {
    constexpr std::string_view scheme = "Bearer ";
    if (header.substr(0, scheme.size()) != scheme) return false;
    return constantTimeEquals(header.substr(scheme.size()), token);
}

inline void rejectRequest(httplib::Response& res, int status, const std::string& why) {
    res.status = status;
    res.set_content(json{{"error", why}}.dump(), "application/json");
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
inline bool checkRequest(const httplib::Request& req, httplib::Response& res, bool requireJson) {
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

} // namespace detail

// Start HTTP server.
//   port  : 0 = OS auto-assign, else fixed port
//   host  : "127.0.0.1" (default) keeps it loopback-only. The default is an
//           address, not "localhost": what "localhost" resolves to differs
//           between OSes, so the default is one address everywhere. Pass
//           "localhost" or "::1" for those, or "0.0.0.0" to expose on all
//           interfaces.
//   token : bearer token required on /mcp. MUST be non-empty when host is not
//           a loopback address — binding non-local without a token is refused
//           (fail-closed) so input injection is never silently network-exposed.
inline void startHttpServer(int port = 0, const std::string& host = "127.0.0.1",
                            const std::string& token = "") {
    auto& svr = detail::getHttpServer();
    if (svr) return; // Already running

    const bool isLoopback = (host == "localhost" || host == "127.0.0.1" || host == "::1");
    if (!isLoopback && token.empty()) {
        trussc::logError("MCP")
            << "Refusing to bind MCP server to non-local host '" << host
            << "' without a token. Set TRUSSC_MCP_TOKEN to expose it (the MCP "
               "surface can inject input and mutate the scene).";
        return;
    }
    detail::mcpAuthToken() = token;
    detail::mcpLoopbackOnly().store(isLoopback);

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
        if (!detail::checkRequest(req, res, true)) return;

        // Bearer auth when a token is configured (always so for non-loopback).
        const std::string& tok = detail::mcpAuthToken();
        if (!tok.empty()) {
            auto it = req.headers.find("Authorization");
            if (it == req.headers.end() || !detail::bearerTokenMatches(it->second, tok)) {
                res.status = 401;
                res.set_content("{\"error\":\"unauthorized\"}", "application/json");
                return;
            }
        }

        auto p = std::make_shared<std::promise<detail::ReplyThunk>>();
        auto f = p->get_future();

        McpRequest mcpReq;
        mcpReq.body = req.body;
        mcpReq.response = p;

        detail::getHttpChannel().send(std::move(mcpReq));

        // Block until main thread processes the request, then execute the
        // reply thunk HERE — heavy two-stage work (thumbnail encode etc.) runs
        // on this worker thread, not the frame loop.
        detail::ReplyThunk thunk = f.get();
        std::string result;
        try {
            result = thunk();
        } catch (const std::exception& e) {
            result = std::string("{\"error\":\"reply construction failed: ") + e.what() + "\"}";
        }

        res.set_content(result, "application/json");
    });

    // GET / — Server info
    svr->Get("/", [](const httplib::Request& req, httplib::Response& res) {
        if (!detail::checkRequest(req, res, false)) return;
        json info = {
            {"name", "TrussC MCP Server"},
            {"transport", "http"},
            {"endpoint", "/mcp"}
        };
        res.set_content(info.dump(), "application/json");
    });

    detail::getHttpThread() = std::make_unique<std::thread>([port, host]() {
        auto& svr = detail::getHttpServer();
        if (!svr) return;

        int actualPort = 0;
        if (port > 0) {
            // Bind to specific port
            if (!svr->bind_to_port(host.c_str(), port)) {
                trussc::logError("MCP") << "Failed to bind HTTP server to " << host << ":" << port;
                return;
            }
            actualPort = port;
        } else {
            // Let OS assign a free port
            actualPort = svr->bind_to_any_port(host.c_str());
            if (actualPort < 0) {
                trussc::logError("MCP") << "Failed to bind HTTP server to any port on " << host;
                return;
            }
        }
        detail::getHttpPort().store(actualPort);

        // The only line with the actual port (an OS-assigned port is known
        // only here, after bind). Through the Logger so it also reaches the
        // log file and onLog listeners: "[MCP] HTTP server listening on
        // http://HOST:PORT/mcp" at Notice (stdout; hidden when the console
        // level is Warning or higher). Set TRUSSC_MCP_PORT for a known port.
        trussc::logNotice("MCP") << "HTTP server listening on http://" << host
                                 << ":" << actualPort << "/mcp";
        // The raw stderr copy stays for v0.7 so tools that read stderr keep
        // working; it is removed in v0.8.0 (#414).
        std::cerr << "[MCP] HTTP server listening on http://" << host << ":" // log-check: allow (#414)
                  << actualPort << "/mcp" << std::endl;

        svr->listen_after_bind();
    });
}

// Stop HTTP server
inline void stopHttpServer() {
    // Unblock any HTTP workers still waiting on a deferred reply — no more frames
    // will be presented, so their producers would never run (and a blocked
    // worker would stall the server shutdown below).
    {
        auto& list = detail::deferredResponses();
        for (auto& d : list) {
            const std::string message = "the MCP server shut down before the reply was produced";
            std::string reply = d.errorReply ? d.errorReply(message)
                                             : "{\"error\":\"" + message + "\"}";
            d.response->set_value([reply]() { return reply; });
        }
        list.clear();
    }

    auto& svr = detail::getHttpServer();
    if (svr) {
        svr->stop();
    }

    detail::getHttpChannel().close();

    auto& t = detail::getHttpThread();
    if (t && t->joinable()) {
        t->join();
    }
    t.reset();
    svr.reset();
}

// Process HTTP request queue (call every frame from main thread)
inline void processHttpQueue() {
    McpRequest req;
    while (detail::getHttpChannel().tryReceive(req)) {
        auto& ds = detail::deferralState();
        ds.hasEnvelope = false;
        ds.target = nullptr;
        ds.timeoutReply = nullptr;
        ds.errorReply = nullptr;
        ds.owner = nullptr;
        std::string result = Server::instance().processMessage(req.body);
        if (ds.hasEnvelope) {
            // Tool deferred its reply until after present(): stash the promise
            // and answer it from drainDeferredResponses(). The HTTP worker stays
            // blocked on its future a few ms longer (correct, not a hang).
            detail::DeferredResponse d;
            d.response = req.response;
            d.makeEnvelope = std::move(ds.envelope);
            d.target = ds.target;
            d.deadline = std::chrono::steady_clock::now() + detail::kTargetedDeferralTimeout;
            d.timeoutReply = std::move(ds.timeoutReply);
            d.owner = ds.owner;
            d.errorReply = std::move(ds.errorReply);
            detail::deferredResponses().push_back(std::move(d));
            ds.hasEnvelope = false;
            ds.target = nullptr;
            continue;
        }
        if (result.empty()) {
            // Return empty JSON-RPC response for notifications
            result = "{}";
        }
        req.response->set_value([result]() { return result; });
    }
}

// Get the actual HTTP port (0 if not started)
inline int getHttpPort() {
    return detail::getHttpPort().load();
}

// Deprecated: registerControlTools() is now the opt-in by itself (it sets the
// isDebuggerEnabled() flag). This shim is a no-op kept for source compatibility.
[[deprecated("registerControlTools() now opts in by itself; remove this call. Will be removed in v1.0.0")]]
inline void enableDebugger() {}

#endif // __EMSCRIPTEN__

namespace detail {
inline void removeRegistrationsOwnedBy(const void* owner) {
    if (!owner) return;
    Server::instance().removeOwnedBy(owner);
    for (auto& hook : ownerCleanupHooks()) hook(owner);
    // Deferred replies whose producers run this owner's code: answer them
    // now, with an error, instead of running them at the next drain, after
    // the App they may reach has been deleted (a reload runs between
    // processHttpQueue() and drainDeferredResponses() in one frame). At exit
    // stopHttpServer() runs before the guest is unloaded and has already
    // answered every pending reply. Host tools' deferrals stay pending.
    auto& pending = deferredResponses();
    std::vector<DeferredResponse> keep;
    for (auto& d : pending) {
        if (d.owner != owner) {
            keep.push_back(std::move(d));
            continue;
        }
        const std::string message = "the app code behind this reply was unloaded by a hot reload "
                                    "before the reply was produced";
        std::string reply = d.errorReply ? d.errorReply(message)
                                         : "{\"error\":\"" + message + "\"}";
        d.response->set_value([reply]() { return reply; });
    }
    pending.swap(keep);
}
} // namespace detail

// Whether a tool with this name is currently registered.
inline bool hasTool(const std::string& name) {
    return Server::instance().hasTool(name);
}

// ---------------------------------------------------------------------------
// Argument Type Traits & Builder Helpers
// ---------------------------------------------------------------------------

template<typename T> struct TypeName { static constexpr const char* value = "string"; };
template<> struct TypeName<int> { static constexpr const char* value = "integer"; };
template<> struct TypeName<float> { static constexpr const char* value = "number"; };
template<> struct TypeName<double> { static constexpr const char* value = "number"; };
template<> struct TypeName<bool> { static constexpr const char* value = "boolean"; };
template<> struct TypeName<json> { static constexpr const char* value = "object"; };

// Tool Builder
class ToolBuilder {
public:
    ToolBuilder(const std::string& name, const std::string& desc) {
        tool_.name = name;
        tool_.description = desc;
    }

    template<typename T>
    ToolBuilder& arg(const std::string& name, const std::string& desc, bool required = true) {
        tool_.args.push_back({name, TypeName<T>::value, desc, required});
        return *this;
    }

    // Simple bind: function receives (const json& args)
    void bind(std::function<json(const json&)> func) {
        tool_.handler = func;
        Server::instance().registerTool(tool_);
    }

    // Typed bind helpers (up to 4 args for simplicity)

    // 0 args
    void bind(std::function<json()> func) {
        tool_.handler = [func](const json&) { return func(); };
        Server::instance().registerTool(tool_);
    }

    // 1 arg
    template<typename T1>
    void bind(std::function<json(T1)> func) {
        auto argName1 = tool_.args[0].name;
        tool_.handler = [func, argName1](const json& args) {
            return func(args.at(argName1).get<T1>());
        };
        Server::instance().registerTool(tool_);
    }

    // 2 args
    template<typename T1, typename T2>
    void bind(std::function<json(T1, T2)> func) {
        auto argName1 = tool_.args[0].name;
        auto argName2 = tool_.args[1].name;
        tool_.handler = [func, argName1, argName2](const json& args) {
            return func(args.at(argName1).get<T1>(), args.at(argName2).get<T2>());
        };
        Server::instance().registerTool(tool_);
    }

    // 3 args
    template<typename T1, typename T2, typename T3>
    void bind(std::function<json(T1, T2, T3)> func) {
        auto a1 = tool_.args[0].name;
        auto a2 = tool_.args[1].name;
        auto a3 = tool_.args[2].name;
        tool_.handler = [func, a1, a2, a3](const json& args) {
            return func(args.at(a1).get<T1>(), args.at(a2).get<T2>(), args.at(a3).get<T3>());
        };
        Server::instance().registerTool(tool_);
    }

private:
    Tool tool_;
};

// Resource Builder
class ResourceBuilder {
public:
    ResourceBuilder(const std::string& uri, const std::string& name) {
        res_.uri = uri;
        res_.name = name;
    }

    ResourceBuilder& desc(const std::string& d) {
        res_.description = d;
        return *this;
    }

    ResourceBuilder& mime(const std::string& m) {
        res_.mimeType = m;
        return *this;
    }

    void bind(std::function<std::string()> func) {
        res_.handler = func;
        Server::instance().registerResource(res_);
    }

private:
    Resource res_;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

inline ToolBuilder tool(const std::string& name, const std::string& desc) {
    return ToolBuilder(name, desc);
}

inline ResourceBuilder resource(const std::string& uri, const std::string& name) {
    return ResourceBuilder(uri, name);
}

} // namespace mcp
} // namespace trussc
