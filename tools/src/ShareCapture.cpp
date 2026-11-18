#include "TrussC.h"
#include "Share.h"
#include "tc/utils/tcProcess.h"
#include "impl/httplib.h"
#include <atomic>
#include <chrono>
#include <charconv>
#include <fstream>
#include <thread>
#ifdef _WIN32
#include <shellapi.h>
#endif

namespace share {
namespace {
// Process inherits the environment. Restore it immediately after spawning.
struct Environment {
    std::string name, previous;
    bool existed;
    Environment(const char* key, const char* value) : name(key), existed(std::getenv(key) != nullptr) {
        if (existed) previous = std::getenv(key);
        set(value);
    }
    void set(const char* value) {
#ifdef _WIN32
        _putenv_s(name.c_str(), value ? value : "");
#else
        if (value) setenv(name.c_str(), value, 1);
        else unsetenv(name.c_str());
#endif
    }
    ~Environment() { set(existed ? previous.c_str() : nullptr); }
};
}
bool captureNative(const fs::path& project, const fs::path& binary,
                   const fs::path& destination, std::string& error) {
    using namespace std::chrono_literals;
    fs::path executable = binary;
    if (const char* fixture = std::getenv("TRUSSC_INTERNAL_SHARE_NATIVE_BIN")) executable = fixture;
    std::atomic<int> port{0};
    tc::Process app;
    auto listener = app.onLine.listen([&](auto& event) {
        // Reuse the MCP server's actual OS-assigned port, announced after bind.
        const std::string prefix = "HTTP server listening on http://127.0.0.1:";
        const auto address = event.line.find(prefix);
        if (address != std::string::npos) {
            const auto start = address + prefix.size();
            const auto end = event.line.find("/mcp", start);
            if (end == std::string::npos) return;
            const char* first = event.line.data() + start;
            const char* last = event.line.data() + end;
            int value = 0;
            const auto parsed = std::from_chars(first, last, value);
            // Bound (port): MCP announcements must name a TCP port in 1–65535.
            if (parsed.ec == std::errc() && parsed.ptr == last && value >= 1 && value <= 65535)
                port.store(value);
        }
    });
    {
        Environment enabled("TRUSSC_MCP", "1"), host("TRUSSC_MCP_HOST", "127.0.0.1"),
                    assignedPort("TRUSSC_MCP_PORT", "0"), token("TRUSSC_MCP_TOKEN", "");
        if (!app.startAsync({executable.string()}, project)) {
            error = "could not launch native app";
            return false;
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!port.load() && app.isRunning() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(50ms);
    bool captured = false;
    if (port.load()) {
        httplib::Client client("127.0.0.1", port.load());
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(10, 0);
        client.set_write_timeout(2, 0);
        auto call = [&](const std::string& tool) {
            nlohmann::json request{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                                   {"params", {{"name", tool}, {"arguments", nlohmann::json::object()}}}};
            return client.Post("/mcp", request.dump(), "application/json");
        };
        std::this_thread::sleep_for(3s); // Allow the app's opening animation to settle.
        try {
            auto response = call("tc_get_screenshot");
            if (!response || response->status != 200) throw std::runtime_error("MCP screenshot request failed or timed out");
            const auto reply = nlohmann::json::parse(response->body);
            if (reply.contains("error"))
                throw std::runtime_error(reply["error"].value("message", "MCP screenshot request failed"));
            if (!reply.contains("result") || !reply["result"].contains("content"))
                throw std::runtime_error("MCP screenshot unavailable (native build needs --mcp)");
            for (const auto& item : reply["result"]["content"]) {
                if (item.value("type", "") == "text") {
                    const auto detail = nlohmann::json::parse(item.value("text", ""), nullptr, false);
                    if (detail.is_object() && detail.contains("message") && detail["message"].is_string())
                        error = detail["message"].get<std::string>();
                }
                if (item.value("type", "") != "image" || item.value("mimeType", "") != "image/png") continue;
                const auto bytes = tc::fromBase64(item.at("data").get<std::string>());
                if (bytes.size() < 8 || std::string(bytes.begin(), bytes.begin() + 8) != std::string("\x89PNG\r\n\x1a\n", 8))
                    throw std::runtime_error("MCP returned an invalid PNG");
                std::ofstream file(destination, std::ios::binary);
                file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!file) throw std::runtime_error("cannot save screenshot");
                captured = true;
                break;
            }
            if (!captured && error.empty()) error = "MCP did not return a screenshot";
        } catch (const std::exception& failure) { error = failure.what(); }
        call("tc_quit");
    } else error = "native app exited or MCP startup timed out (permission dialog may be open)";
    const auto quitDeadline = std::chrono::steady_clock::now() + 3s;
    while (app.isRunning() && std::chrono::steady_clock::now() < quitDeadline)
        std::this_thread::sleep_for(50ms);
    if (app.isRunning()) app.kill(); // Process tracks only this launch's PID/process group/job.
    app.waitForExit();
    return captured;
}
void viewScreenshot(const fs::path& path) {
#ifdef _WIN32
    // Use the desktop association, equivalent to start, without cmd quoting.
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    auto viewer = std::make_unique<tc::Process>();
#ifdef __APPLE__
    std::vector<std::string> args{"open", path.string()};
#else
    std::vector<std::string> args{"xdg-open", path.string()};
#endif
    if (!viewer->startAsync(args)) return;
    // Reap the helper off the prompt thread, allowing the desktop viewer to stay open.
    viewer->setKillOnDestroy(false);
    std::thread([process = std::move(viewer)] { process->waitForExit(); }).detach();
#endif
}
} // namespace share
