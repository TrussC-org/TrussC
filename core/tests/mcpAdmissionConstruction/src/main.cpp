#include <TrussC.h>
#include "../../../include/impl/httplib.h"
#include "../../common/tcCoreTest.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>

#ifdef TC_ADMISSION_CAPTURE_PROBE
extern "C" void* __real_malloc(size_t);
extern "C" unsigned char* stbi_write_png_to_mem(const unsigned char*, int, int, int, int, int*);
#endif
// This test owns its binary: interposition must not affect other harnesses.
namespace {
thread_local bool failNextAllocation = false;
thread_local int failAllocationAfter = -1;
#ifdef TC_ADMISSION_CAPTURE_PROBE
thread_local size_t encoderMallocSize = 0;
thread_local int encoderMallocMatches = 0;
thread_local bool failEncoderMalloc = false;
std::atomic<bool> failParsingOnce{false};
std::atomic<size_t> serverBytesRead{0};
std::atomic<bool> countServerReads{false};
#endif
int checks = 0, failures = 0;
void check(const char* name, bool ok) {
    ++checks;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
}
void* operator new(std::size_t n) {
    if (failAllocationAfter >= 0 && failAllocationAfter-- == 0) { failAllocationAfter = -1; throw std::bad_alloc(); }
    if (failNextAllocation) { failNextAllocation = false; throw std::bad_alloc(); }
#ifdef TC_ADMISSION_CAPTURE_PROBE
    if (auto* p = __real_malloc(n ? n : 1)) return p;
#else
    if (auto* p = std::malloc(n ? n : 1)) return p;
#endif
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t& tag) noexcept { return ::operator new(n, tag); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#ifdef TC_ADMISSION_CAPTURE_PROBE
#include <GL/gl.h>
extern "C" void* __wrap_malloc(size_t size) {
    if (encoderMallocSize != 0 && size == encoderMallocSize) {
        ++encoderMallocMatches;
        if (failEncoderMalloc) {
            failEncoderMalloc = false;
            return nullptr;
        }
    }
    return __real_malloc(size);
}
extern "C" ssize_t __real_recv(int, void*, size_t, int);
extern "C" ssize_t __wrap_recv(int sock, void* data, size_t size, int flags) {
    const auto n = __real_recv(sock, data, size, flags);
    if (n > 0 && countServerReads && !(flags & MSG_PEEK)) {
        sockaddr_in local{};
        socklen_t length = sizeof(local);
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&local), &length) == 0 &&
            ntohs(local.sin_port) == trussc::mcp::getHttpPort()) serverBytesRead += size_t(n);
    }
    if (n > 0 && failParsingOnce.exchange(false)) failNextAllocation = true;
    return n;
}
extern "C" bool __wrap_sg_isvalid() { return true; }
extern "C" void __wrap_sg_tc_submit_prefix() {}
extern "C" int __wrap_sapp_width() { return 3840; }
extern "C" int __wrap_sapp_height() { return 2160; }
extern "C" void __wrap_glReadPixels(GLint, GLint, GLsizei w, GLsizei h, GLenum, GLenum, void* data) {
    std::memset(data, 127, size_t(w) * h * 4);
}
#endif
namespace trussc::mcp::internal {
json pixelsToImageJson(const Pixels&, const std::string&, int, int);
}
using namespace tc;
namespace {
std::string request(const char* name, json args = json::object()) {
    return json{{"jsonrpc", "2.0"}, {"id", 19}, {"method", "tools/call"},
        {"params", {{"name", name}, {"arguments", std::move(args)}}}}.dump();
}
json dispatch(const std::string& body, bool deferred = false) {
    using namespace mcp::internal;
    auto promise = std::make_shared<std::promise<ReplyThunk>>();
    auto future = promise->get_future();
    getHttpChannel().send(mcp::McpRequest{body, promise});
    mcp::processHttpQueue();
    if (deferred) drainDeferredResponses();
    return json::parse(future.get()());
}
json content(const json& reply) {
    return json::parse(reply.at("result").at("content").at(0).at("text").get<std::string>());
}
} // namespace
TC_CORE_TEST_MAIN() {
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    mcp::stopHttpServer();
    mcp::internal::resetHttpChannel();
    const auto dir = fs::temp_directory_path() / ("mcp-now-works-" + std::to_string(getSystemTimeMicros()));
    fs::create_directories(dir);
    setDataPathRoot(dir);
#ifdef TC_ADMISSION_CAPTURE_PROBE
    // Existing headless framebuffer seam exercises the production MCP save path.
    const auto saved = content(dispatch(request("tc_save_screenshot", {{"path", "4k.png"}}), true));
    check("3840x2160 MCP screenshot save succeeds", saved.value("status", "") == "ok");
    Pixels screenshot;
    check("saved screenshot has 4K dimensions", screenshot.load(dir / "4k.png") &&
        screenshot.getWidth() == 3840 && screenshot.getHeight() == 2160);
#endif
    {
        Pixels source;
        source.allocate(2304, 2304, 4);
        std::memset(source.getData(), 127, source.getTotalBytes());
        check("large local image saved", bool(source.save(dir / "large.png")));
    }
    auto analyzed = content(dispatch(request("tc_analyze_image", {{"source", {{"path", "large.png"}}},
        {"ops", json::array({{{"op", "pixel"}, {"x", 2303}, {"y", 2303}}})}})));
    check("local image above 2048x2048 is analyzed", analyzed["width"] == 2304 && analyzed["height"] == 2304 && analyzed.contains("results"));
    // Fail an actual allocation once, so error reporting itself can allocate.
    mcp::tool("allocation_pixels", "pixel allocation failure").bind([]() -> json {
        failNextAllocation = true;
        Pixels pixels; pixels.allocate(128, 128, 4);
        return {{"unexpected", true}};
    });
    mcp::tool("allocation_json", "JSON allocation failure").bind([]() -> json {
        failNextAllocation = true;
        return json::array({1, 2, 3});
    });
    mcp::tool("allocation_main", "deferred main allocation failure").bind([]() -> json {
        mcp::deferToolResultUntilAfterFrame([]() -> json {
            failNextAllocation = true;
            return json::array({1, 2, 3});
        });
        return nullptr;
    });
    mcp::tool("allocation_worker", "deferred worker allocation failure").bind([]() -> json {
        mcp::deferToolResultTwoStage([]() -> std::function<json()> {
            return []() -> json {
                failNextAllocation = true;
                return json::array({1, 2, 3});
            };
        });
        return nullptr;
    });
    mcp::tool("allocation_envelope", "deferred envelope allocation failure").bind([]() -> json {
        mcp::deferToolResultUntilAfterFrame([]() -> json { return nullptr; });
        failNextAllocation = true;
        return nullptr;
    });
    for (const char* name : {"allocation_pixels", "allocation_json", "allocation_main", "allocation_worker", "allocation_envelope"}) {
        const auto reply = dispatch(request(name), true);
        check(name, (reply.contains("error") || reply.at("result").value("isError", false)) && reply.at("id") == 19);
        auto stringRequest = json::parse(request(name));
        stringRequest["id"] = "request-\"19";
        const auto stringReply = dispatch(stringRequest.dump(), true);
        check("allocation error preserves string id through deferred stages", stringReply.at("id") == stringRequest.at("id") && stringReply.contains("error"));
        const auto next = json::parse(mcp::internal::Server::instance().processMessage(
            R"({"jsonrpc":"2.0","id":42,"method":"initialize"})"));
        check("server continues after allocation failure", next["id"] == 42 && next.contains("result"));
    }
    for (const json& id : {json(19), json("request-\"19"), json(-42)}) {
        auto message = json::parse(request("allocation_json"));
        message["id"] = id;
        const auto reply = json::parse(mcp::internal::Server::instance().processMessage(message.dump()));
        check("synchronous allocation error preserves numeric/string id", reply.at("id") == id && reply.contains("error"));
    }
    {
        const auto message = request("allocation_json");
        failNextAllocation = true;
        const auto reply = json::parse(mcp::internal::Server::instance().processMessage(message));
        check("allocation before parsing establishes id uses null", reply.at("id").is_null() && reply.contains("error"));
    }
    // Exercise compressed and incompressible input through normal PNG save/load.
    for (bool random : {false, true}) {
        Pixels original; original.allocate(257, 129, 4);
        unsigned seed = 1234567;
        for (size_t i = 0; i < original.getTotalBytes(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            original.getData()[i] = random ? static_cast<unsigned char>(seed >> 24) : 127;
        }
        const auto path = dir / (random ? "random.png" : "uniform.png");
        Pixels decoded;
        check("PNG compressor roundtrip preserves pixel bytes", bool(original.save(path)) && bool(decoded.load(path)) &&
            decoded.getTotalBytes() == original.getTotalBytes() &&
            std::memcmp(decoded.getData(), original.getData(), original.getTotalBytes()) == 0);
    }
    // Each destination reserve can fail, including after earlier collections allocated.
    // Retain every promise and producer, then retry the same pending entries.
    for (int allocation = 0; allocation < 3; ++allocation) {
        using namespace mcp::internal;
        std::vector<std::future<ReplyThunk>> futures;
        auto producer = std::make_shared<int>(1);
        std::weak_ptr<int> lifetime = producer;
        for (int i = 0; i < 3; ++i) {
            DeferredResponse d;
            d.id = 19 + i;
            d.response = std::make_shared<std::promise<ReplyThunk>>();
            futures.push_back(d.response->get_future());
            d.makeEnvelope = [producer] { return [] { return std::string("ok"); }; };
            d.target = i == 0 ? nullptr : reinterpret_cast<void*>(1);
            d.deadline = i == 1 ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point::max();
            deferredResponses().push_back(std::move(d));
        }
        producer.reset();
        (void)deferredResponsesMutex();
        failAllocationAfter = allocation;
        drainDeferredResponses();
        bool intact = deferredResponses().size() == 3 && !lifetime.expired();
        for (const auto& d : deferredResponses()) intact = intact && bool(d.response) && bool(d.makeEnvelope);
        for (auto& f : futures) intact = intact && f.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
        check("partition allocation failure retains all pending producers/promises", intact && failAllocationAfter == -1);
        drainDeferredResponses();
        drainDeferredResponses(reinterpret_cast<void*>(1));
        bool resolved = true;
        for (auto& f : futures) {
            resolved = resolved && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            if (resolved) (void)f.get()();
        }
        check("partition retry resolves all requests and releases producers", resolved && lifetime.expired());
    }
#ifdef TC_ADMISSION_CAPTURE_PROBE
    // Calibrate the final buffer size with the same input/settings. A malloc
    // index is unstable: optimization can turn compressor realloc(NULL) into malloc.
    Pixels encoderPixels; encoderPixels.allocate(64, 64, 4);
    std::memset(encoderPixels.getData(), 127, encoderPixels.getTotalBytes());
    int pngSize = 0;
    auto* encoded = stbi_write_png_to_mem(encoderPixels.getData(), 0, 64, 64, 4, &pngSize);
    check("PNG allocation probe obtains final buffer size", encoded && pngSize > 0);
    std::free(encoded);
    if (!encoded || pngSize <= 0) return 1;
    // Filtered pixels, line storage, final PNG; zero selects the C++ output copy.
    for (size_t allocationSize : {size_t((64 * 4 + 1) * 64), size_t(64 * 4), size_t(pngSize), size_t(0)}) {
        if (allocationSize != 0) {
            encoderMallocSize = allocationSize;
            encoderMallocMatches = 0;
            (void)mcp::internal::pixelsToImageJson(encoderPixels, "png", 0, 90);
            encoderMallocSize = 0;
            // Refuse an ambiguous predicate rather than failing compressor growth.
            check("PNG size predicate uniquely identifies its allocation", encoderMallocMatches == 1);
            if (encoderMallocMatches != 1) return 1;
        }
        // Select inside the handler: dispatch must retain a parsed request id.
        mcp::tool("allocation_png", "encoder malloc failure").bind([allocationSize, &encoderPixels]() -> json {
            encoderMallocMatches = 0;
            if (allocationSize == 0) failNextAllocation = true;
            else {
                encoderMallocSize = allocationSize;
                failEncoderMalloc = true;
            }
            return mcp::internal::pixelsToImageJson(encoderPixels, "png", 0, 90);
        });
        const auto reply = dispatch(request("allocation_png"));
        encoderMallocSize = 0;
        check("PNG allocation failure injection was consumed", allocationSize == 0 ? !failNextAllocation :
            !failEncoderMalloc && encoderMallocMatches == 1);
        check("PNG encoder allocation failure returns an MCP error with id", reply.at("id") == 19 &&
            reply.contains("error"));
    }
#endif
    // Repeat through actual HTTP workers, including deferred worker construction.
    mcp::startHttpServer();
    {
        std::vector<std::shared_ptr<void>> credits;
        for (int i = 0; i < 8; ++i) credits.push_back(mcp::internal::acquireScreenshotCredit());
        auto body = json::parse(request("tc_save_screenshot", {{"path", "refused.png"}}));
        body["id"] = 37;
        const auto direct = dispatch(body.dump());
        check("screenshot slot refusal preserves id in JSON-RPC error", direct.at("id") == 37 && direct.contains("error"));
        auto pending = std::async(std::launch::async, [body = body.dump()] {
            httplib::Client client("127.0.0.1", mcp::getHttpPort());
            client.set_read_timeout(10);
            return client.Post("/mcp", body, "application/json");
        });
        while (pending.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) mcp::processHttpQueue();
        const auto response = pending.get();
        check("HTTP screenshot slot refusal returns JSON-RPC error with id 37", response && response->status == 200 &&
            json::parse(response->body).at("id") == 37 && json::parse(response->body).contains("error"));
        check("refused screenshot creates no file or deferred capture", !fs::exists(dir / "refused.png") && !mcp::internal::hasDeferredResponses());
    }
#ifdef TC_ADMISSION_CAPTURE_PROBE
    {
        serverBytesRead = 0;
        countServerReads = true;
        const auto sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(mcp::getHttpPort());
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        check("request-line probe connects", connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        timeval timeout{3, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        const std::string wire = "GET /" + std::string(1024 * 1024, 'a') + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        size_t sent = 0;
        while (sent < wire.size()) {
            const auto n = send(sock, wire.data() + sent, wire.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) break;
            sent += size_t(n);
        }
        std::string response;
        char buffer[1024];
        ssize_t received;
        while ((received = recv(sock, buffer, sizeof(buffer), 0)) > 0) response.append(buffer, size_t(received));
        const bool closed = received == 0 || errno == ECONNRESET;
        close(sock);
        countServerReads = false;
        check("1 MiB request line is refused without reading it all", closed && response.find("414") != std::string::npos &&
            response.find("HTTP/1.1", 1) == std::string::npos && serverBytesRead > 0 && serverBytesRead <= 16384);
        printf("request-line server bytes read: %zu\n", serverBytesRead.load());
    }
    {
        serverBytesRead = 0;
        countServerReads = true;
        const auto sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(mcp::getHttpPort());
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        check("header probe connects", connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        timeval timeout{3, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        std::string wire = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n";
        for (int i = 0; i < 1024; ++i) wire += "X-Probe: " + std::string(1024, 'a') + "\r\n";
        wire += "\r\n";
        size_t sent = 0;
        while (sent < wire.size()) {
            const auto n = send(sock, wire.data() + sent, wire.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) break;
            sent += size_t(n);
        }
        std::string response;
        char buffer[1024];
        ssize_t received;
        while ((received = recv(sock, buffer, sizeof(buffer), 0)) > 0) response.append(buffer, size_t(received));
        const bool closed = received == 0 || errno == ECONNRESET;
        close(sock);
        countServerReads = false;
        // Bound (network input): this short request line and 64 KiB header cap fit within one extra read-ahead block.
        check("oversized headers are refused without reading beyond the header bound", closed && response.find("431") != std::string::npos &&
            response.find("HTTP/1.1", 1) == std::string::npos && serverBytesRead > 0 && serverBytesRead <= 64 * 1024 + 4096);
        printf("header server bytes read: %zu\n", serverBytesRead.load());
    }
#endif
    for (const char* name : {"allocation_pixels", "allocation_json", "allocation_main", "allocation_worker", "allocation_envelope"}) {
        auto pending = std::async(std::launch::async, [body = request(name)] {
            httplib::Client client("127.0.0.1", mcp::getHttpPort());
            client.set_read_timeout(10);
            auto result = client.Post("/mcp", body, "application/json");
            return result && result->status == 200 && json::parse(result->body).contains("error") &&
                json::parse(result->body).at("id") == 19;
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (mcp::internal::getHttpChannel().size() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        mcp::processHttpQueue();
        mcp::internal::drainDeferredResponses();
        check("HTTP allocation failure returns an MCP error", pending.get());
        httplib::Client health("127.0.0.1", mcp::getHttpPort());
        auto response = health.Get("/");
        check("HTTP worker remains usable", response && response->status == 200);
    }
#ifdef TC_ADMISSION_CAPTURE_PROBE
    // Fail outside upstream's routing catch, using the production task queue/socket entry.
    for (bool parsing : {false, true}) {
        if (parsing) failParsingOnce = true;
        else mcp::internal::getHttpServer()->set_post_routing_handler([](const auto&, auto&) { failNextAllocation = true; });
        httplib::Client failed("127.0.0.1", mcp::getHttpPort());
        failed.set_read_timeout(3);
        check("socket boundary closes after actual parsing/response bad_alloc", !failed.Get("/"));
        mcp::internal::getHttpServer()->set_post_routing_handler(nullptr);
        httplib::Client health("127.0.0.1", mcp::getHttpPort());
        const auto response = health.Get("/");
        check("production worker survives socket-boundary bad_alloc", response && response->status == 200);
    }
#endif
    mcp::stopHttpServer();
    fs::remove_all(dir);
    return failures ? 1 : 0;
}
