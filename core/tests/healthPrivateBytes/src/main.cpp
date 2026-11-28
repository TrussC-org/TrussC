// Private/RSS API and MCP regression: privateBytes on every native platform, Linux RssAnon, POSIX file mmap, and health-only HTTP.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#ifndef __EMSCRIPTEN__
#include <future>
#include "impl/httplib.h"
#endif
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace tc;

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
json health() {
    const auto reply = json::parse(mcp::internal::Server::instance().processMessage(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"tc_get_health","arguments":{}}})"));
    return json::parse(reply.at("result").at("content").at(0).at("text").get<std::string>());
}
struct HealthApp : App {
#ifndef __EMSCRIPTEN__
    std::future<httplib::Result> response;
#endif
    void setup() override {
        check("health-only grant remains Public", mcp::getToolLevel() == mcp::ToolLevel::Public);
        const auto snapshot = health();
        check("headless health succeeds", snapshot.value("status", "") == "ok");
#ifndef __EMSCRIPTEN__
        check("native health includes nonzero privateBytes",
              snapshot.contains("privateBytes") && snapshot.at("privateBytes").get<int64_t>() > 0);
        check("native getter returns nonzero private memory", getMemoryUsage() > 0);
#endif
#if defined(__EMSCRIPTEN__)
        check("Web omits privateBytes", !snapshot.contains("privateBytes"));
        check("Web getter returns zero", getMemoryUsage() == 0);
#elif defined(__linux__)
        check("Linux health includes privateBytes", snapshot.contains("privateBytes"));
        if (!snapshot.contains("privateBytes")) { exitApp(); return; }
        std::ifstream status("/proc/self/status");
        std::string line;
        int64_t manualBytes = -1;
        while (std::getline(status, line)) {
            if (line.compare(0, 8, "RssAnon:") == 0) {
                std::istringstream value(line.substr(8));
                value >> manualBytes;
                manualBytes *= 1024;
            }
        }
        const auto measured = snapshot.at("privateBytes").get<int64_t>();
        std::printf("privateBytes=%lld manual RssAnon=%lld\n",
                    (long long)measured, (long long)manualBytes);
        // Other threads in the shared test runner allocate between reads; 8 MiB
        // still catches a unit error (kB vs bytes is a 1024x gap).
        constexpr int64_t slack = 8 * 1024 * 1024;
        check("resident getter agrees with health RSS",
              std::abs(int64_t(getResidentMemoryUsage()) - snapshot.at("rssBytes").get<int64_t>()) <= slack);
        check("public getter agrees with health",
              std::abs(int64_t(getMemoryUsage()) - measured) <= slack);
        check("privateBytes agrees with independent RssAnon read",
              manualBytes >= 0 && std::abs(measured - manualBytes) <= slack);
#endif
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        // POSIX: a shared file mapping adds resident pages but no private memory.
        if (!snapshot.contains("privateBytes")) { exitApp(); return; }
        constexpr size_t fileBytes = 64 * 1024 * 1024;
        FILE* file = std::tmpfile();
        check("create local mapped-file fixture", file != nullptr);
        if (file) {
            char page[4096]{};
            bool written = true;
            for (size_t offset = 0; offset < fileBytes; offset += sizeof(page))
                if (std::fwrite(page, 1, sizeof(page), file) != sizeof(page)) { written = false; break; }
            written = std::fflush(file) == 0 && written;
            check("write mapped-file fixture", written);
            if (written) {
                const auto before = health();
                void* mapping = mmap(nullptr, fileBytes, PROT_READ, MAP_SHARED, fileno(file), 0);
                check("map local file", mapping != MAP_FAILED);
                if (mapping != MAP_FAILED) {
                    volatile unsigned char sum = 0;
                    const auto* bytes = static_cast<const unsigned char*>(mapping);
                    const size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
                    for (size_t offset = 0; offset < fileBytes; offset += pageSize)
                        sum = static_cast<unsigned char>(sum + bytes[offset]);
                    (void)sum;
                    const auto after = health();
                    const auto rssDelta = after.at("rssBytes").get<int64_t>() - before.at("rssBytes").get<int64_t>();
                    const auto privateDelta = after.at("privateBytes").get<int64_t>() - before.at("privateBytes").get<int64_t>();
                    std::printf("mapped bytes=%zu RSS delta=%lld private delta=%lld\n",
                                fileBytes, (long long)rssDelta, (long long)privateDelta);
                    check("file mapping raises RSS", rssDelta >= int64_t(fileBytes / 2));
                    check("private memory stays flat for file mapping", std::abs(privateDelta) <= 8 * 1024 * 1024);
                    munmap(mapping, fileBytes);
                }
            }
            std::fclose(file);
        }
#endif
    }
    void update() override {
#if defined(__EMSCRIPTEN__)
        exitApp();
#else
        if (!response.valid()) {
            check("health-only HTTP endpoint is active", mcp::internal::healthOnly() && mcp::getHttpPort() > 0);
            if (mcp::getHttpPort() <= 0) { exitApp(); return; }
            response = std::async(std::launch::async, [] {
                httplib::Client client("127.0.0.1", mcp::getHttpPort());
                client.set_connection_timeout(5);
                client.set_read_timeout(5);
                return client.Post("/mcp",
                    R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"tc_get_health","arguments":{}}})",
                    "application/json");
            });
        }
        if (response.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        const auto reply = response.get();
        check("health-only HTTP request succeeds", reply && reply->status == 200);
        if (reply && reply->status == 200) {
            const auto body = json::parse(reply->body);
            const auto snapshot = json::parse(body.at("result").at("content").at(0).at("text").get<std::string>());
            check("health-only HTTP returns counters", snapshot.value("status", "") == "ok" && snapshot.contains("rssBytes"));
            check("health-only HTTP includes privateBytes", snapshot.contains("privateBytes"));
        }
        exitApp();
#endif
    }
};
} // namespace

TC_CORE_TEST_MAIN(int, char**) {
#if defined(_WIN32)
    _putenv_s("TRUSSC_MCP", "1");
    _putenv_s("TRUSSC_MCP_PORT", "0");
#elif !defined(__EMSCRIPTEN__)
    setenv("TRUSSC_MCP", "1", 1);
    setenv("TRUSSC_MCP_PORT", "0", 1);
#endif
    runHeadlessApp<HealthApp>();
    return failures ? 1 : 0;
}
