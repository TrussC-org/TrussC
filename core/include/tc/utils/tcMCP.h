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
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <sstream>
#include <future>
#include <thread>
#include <atomic>

// JSON support
#include "../../nlohmann/json.hpp"
using json = nlohmann::json;

#include "tcLog.h"
#include "tcThreadChannel.h"

#ifndef __EMSCRIPTEN__
#include "../../impl/httplib.h"
#endif

namespace trussc {
namespace mcp {

// ---------------------------------------------------------------------------
// Types & Interfaces
// ---------------------------------------------------------------------------

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
};

// ---------------------------------------------------------------------------
// MCP Server Core
// ---------------------------------------------------------------------------

class Server {
public:
    static Server& instance() {
        static Server server;
        return server;
    }

    // --- Registration API ---

    void registerTool(const Tool& tool) {
        tools_[tool.name] = tool;
    }

    void registerResource(const Resource& res) {
        resources_[res.uri] = res;
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
                {"version", "0.0.1"}
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

        try {
            // Execute tool handler
            json content = tools_[name].handler(args);

            // Format result according to MCP spec
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
    std::shared_ptr<std::promise<std::string>> response;
};

namespace detail {

inline ThreadChannel<McpRequest>& getHttpChannel() {
    static ThreadChannel<McpRequest> channel;
    return channel;
}

inline std::unique_ptr<httplib::Server>& getHttpServer() {
    static std::unique_ptr<httplib::Server> svr;
    return svr;
}

inline std::unique_ptr<std::thread>& getHttpThread() {
    static std::unique_ptr<std::thread> t;
    return t;
}

inline std::atomic<int>& getHttpPort() {
    static std::atomic<int> port{0};
    return port;
}

inline std::atomic<bool>& isDebuggerEnabled() {
    static std::atomic<bool> enabled{false};
    return enabled;
}

} // namespace detail

// Start HTTP server on specified port (0 = OS auto-assign)
inline void startHttpServer(int port = 0) {
    auto& svr = detail::getHttpServer();
    if (svr) return; // Already running

    svr = std::make_unique<httplib::Server>();

    // POST /mcp — JSON-RPC requests
    svr->Post("/mcp", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");

        auto p = std::make_shared<std::promise<std::string>>();
        auto f = p->get_future();

        McpRequest mcpReq;
        mcpReq.body = req.body;
        mcpReq.response = p;

        detail::getHttpChannel().send(std::move(mcpReq));

        // Block until main thread processes the request
        std::string result = f.get();

        res.set_content(result, "application/json");
    });

    // OPTIONS /mcp — CORS preflight
    svr->Options("/mcp", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // GET / — Server info
    svr->Get("/", [](const httplib::Request&, httplib::Response& res) {
        json info = {
            {"name", "TrussC MCP Server"},
            {"transport", "http"},
            {"endpoint", "/mcp"}
        };
        res.set_content(info.dump(), "application/json");
    });

    detail::getHttpThread() = std::make_unique<std::thread>([port]() {
        auto& svr = detail::getHttpServer();
        if (!svr) return;

        int actualPort = 0;
        if (port > 0) {
            // Bind to specific port
            if (!svr->bind_to_port("localhost", port)) {
                trussc::logError("MCP") << "Failed to bind HTTP server to port " << port;
                return;
            }
            actualPort = port;
        } else {
            // Let OS assign a free port
            actualPort = svr->bind_to_any_port("localhost");
            if (actualPort < 0) {
                trussc::logError("MCP") << "Failed to bind HTTP server to any port";
                return;
            }
        }
        detail::getHttpPort().store(actualPort);

        std::cerr << "[MCP] HTTP server listening on http://localhost:"
                  << actualPort << "/mcp" << std::endl;

        svr->listen_after_bind();
    });
}

// Stop HTTP server
inline void stopHttpServer() {
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
        std::string result = Server::instance().processMessage(req.body);
        if (result.empty()) {
            // Return empty JSON-RPC response for notifications
            result = "{}";
        }
        req.response->set_value(result);
    }
}

// Get the actual HTTP port (0 if not started)
inline int getHttpPort() {
    return detail::getHttpPort().load();
}

// Enable debugger tools (mouse, keyboard input injection)
inline void enableDebugger() {
    if (detail::isDebuggerEnabled().load()) return;
    detail::isDebuggerEnabled().store(true);
    // Actual tool registration happens in tcStandardTools.h
    // This is called before registerDebuggerTools()
}

// Check if debugger is enabled
inline bool isDebuggerEnabled() {
    return detail::isDebuggerEnabled().load();
}

#endif // __EMSCRIPTEN__

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
