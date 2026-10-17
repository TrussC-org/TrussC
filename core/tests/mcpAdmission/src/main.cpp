#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../../include/tc/utils/tcAnalyzeImage.h"
#include "../../../include/tc/utils/tcAudioTools.h"
#include "../../../include/impl/httplib.h"
#include "../../../include/impl/tcMcpImageAdmission.h"
#include <cstdio>
#include <fstream>
#ifndef _WIN32
#include <sys/socket.h>
#endif

using namespace tc;
using namespace std;
namespace {
int failures = 0;
struct ReplyNode : Node {
    string value;
    void reflectMembers(Reflector& reflector) override {
        Node::reflectMembers(reflector);
        reflector.visit("value", value);
    }
};
void check(const char* name, bool ok) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
}
TC_CORE_TEST_MAIN() {
    using mcp::internal::AdmissionStore;
    // Operational stores warn once per process; Configuration warns per call
    // and is covered separately by mcpAdmissionConstruction.
    constexpr size_t storeCount = static_cast<size_t>(AdmissionStore::Configuration);
    array<atomic<int>, storeCount> warnings{};
    const char* stores[] = {"body", "headers", "connections", "histogram", "grid", "operations", "reply", "working memory"};
    EventListener listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level != LogLevel::Warning) return;
        for (size_t i = 0; i < storeCount; ++i)
            if (event.message.find(string("[MCP] ") + stores[i] + " capacity ") == 0) ++warnings[i];
    });
    const auto defaults = mcp::getAdmissionLimits();
    check("default admission settings", defaults.bodyBytes == 1024u * 1024 && defaults.jsonDepth == 64 && defaults.outstanding == 32 && defaults.workers == 4 &&
        defaults.lineBytes == 8192 && defaults.headerBytes == 65536 && defaults.headerCount == 128 && defaults.histogramBins == 4096 &&
        defaults.gridCells == 65536 && defaults.operations == 256 && defaults.replyBytes == 64u * 1024 * 1024 && defaults.workingBytes == 256u * 1024 * 1024);
    auto zero = defaults;
    zero.bodyBytes = 0;
    bool invalid = !mcp::setAdmissionLimits(zero);
    check("zero limits leave settings unchanged", invalid && mcp::getAdmissionLimits().bodyBytes == defaults.bodyBytes);
    Pixels pixels;
    pixels.allocate(257, 256, 4);
    auto analyze = [&](json ops) {
        return mcp::internal::analyzeImage(pixels, {{"ops", std::move(ops)}}, "sRGB");
    };
    for (int i = 0; i < 2; ++i) {
        check("excess histogram bins are rejected", analyze(json::array({{{"op", "histogram"}, {"bins", 4097}}})).value("status", "") == "error");
        check("excess grid cells are rejected", analyze(json::array({{{"op", "grid"}, {"cols", 257}, {"rows", 256}}})).value("status", "") == "error");
        json ops = json::array();
        for (int j = 0; j < 257; ++j) ops.push_back({{"op", "pixel"}, {"x", 0}, {"y", 0}});
        check("excess operations are rejected", analyze(std::move(ops)).value("status", "") == "error");
    }
    check("large histogram is rejected before resize", analyze(json::array({{{"op", "histogram"}, {"bins", INT_MAX}}})).value("status", "") == "error");
    check("large grid product is rejected before resize", analyze(json::array({{{"op", "grid"}, {"cols", INT_MAX}, {"rows", INT_MAX}}})).value("status", "") == "error");
    check("histogram boundary is accepted", analyze(json::array({{{"op", "histogram"}, {"bins", 4096}}})).contains("results"));
    check("grid boundary is accepted", analyze(json::array({{{"op", "grid"}, {"cols", 256}, {"rows", 256}}})).contains("results"));
    bool rejected = false;
    try { (void)mcp::internal::dumpReply(json(string(64u * 1024 * 1024 + 1, 'a'))); }
    catch (const exception&) { rejected = true; }
    check("reply bytes are bounded during serialization", rejected);
    auto small = defaults;
    small.replyBytes = 4096;
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(small));
    auto limited = analyze(json::array({{{"op", "grid"}, {"cols", 8}, {"rows", 8}}}));
    check("reply allowance is checked before constructing cells", limited.value("status", "") == "error" && !limited.contains("results"));
    check("analysis releases working credits", mcp::internal::workingMemory() == 0);
    small.replyBytes = 32768;
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(small));
    {
        ReplyNode node;
        node.value.assign(32768, 'a');
        mcp::internal::WorkScope scope;
        bool full = false;
        try { (void)nodeToJson(node); } catch (const mcp::internal::AdmissionError&) { full = true; }
        check("reflected fields are checked before copying", full);
    }
    small = defaults;
    small.workingBytes = 128;
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(small));
    {
        mcp::internal::WorkScope scope;
        mcp::internal::reserveWork(100);
        bool full = false;
        try { mcp::internal::reserveWork(29); } catch (const mcp::internal::AdmissionError&) { full = true; }
        check("aggregate reservation rejects before allocation", full && mcp::internal::workingMemory() == 100);
    }
    check("working credits are released after rejection", mcp::internal::workingMemory() == 0);
    {
        trussc::internal::AudioOutputRing ring(100, 2);
        mcp::internal::WorkScope scope;
        bool full = false;
        try { (void)ring.snapshot(200, &mcp::internal::reserveAudioSnapshot); }
        catch (const mcp::internal::AdmissionError&) { full = true; }
        check("audio snapshot admission precedes allocation", full && mcp::internal::workingMemory() == 0);
    }
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(defaults));
    const string deepRequest = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","extra":)" +
        string(65, '[') + "0" + string(65, ']') + "}";
    check("JSON nesting is limited before dispatch", json::parse(mcp::internal::Server::instance().processMessage(deepRequest)).contains("error") &&
        mcp::internal::workingMemory() == 0);
    {
        const auto path = fs::temp_directory_path() / ("mcp-admission-image-" + to_string(getSystemTimeMicros()) + ".tga");
        array<unsigned char, 18> header{};
        header[2] = 2;
        header[12] = 0x10; header[13] = 0x27;
        header[14] = 0x10; header[15] = 0x27;
        header[16] = 32;
        ofstream file(path, ios::binary);
        file.write(reinterpret_cast<const char*>(header.data()), header.size());
        file.close();
        Pixels loaded;
        bool full = false;
        try { (void)mcp::internal::loadMcpImage(loaded, path); }
        catch (const mcp::internal::AdmissionError&) { full = true; }
        check("image metadata is limited before decoding", full && !loaded.isAllocated() && mcp::internal::workingMemory() == 0);
        {
            mcp::internal::ImageAllocationScope allocation(128);
            int width = 0, height = 0, channels = 0;
            auto* decoded = stbi_load_from_memory(header.data(), int(header.size()), &width, &height, &channels, 4);
            check("codec allocation stops before exceeding workspace", !decoded && allocation.exceeded() && allocation.peak() <= 128);
            stbi_image_free(decoded);
        }
        fs::remove(path);
    }
#ifdef __APPLE__
    {
        // One uncompressed grayscale TIFF pixel; stb has no TIFF decoder.
        vector<unsigned char> tiff(123, 0);
        auto le16 = [&](size_t offset, unsigned value) {
            tiff[offset] = value & 255; tiff[offset + 1] = (value >> 8) & 255;
        };
        auto le32 = [&](size_t offset, unsigned value) {
            for (unsigned i = 0; i < 4; ++i) tiff[offset + i] = (value >> (i * 8)) & 255;
        };
        tiff[0] = 'I'; tiff[1] = 'I'; le16(2, 42); le32(4, 8); le16(8, 9);
        size_t entry = 10;
        auto tag = [&](unsigned key, unsigned type, unsigned value) {
            le16(entry, key); le16(entry + 2, type); le32(entry + 4, 1); le32(entry + 8, value);
            entry += 12;
        };
        tag(256, 4, 1); tag(257, 4, 1); tag(258, 3, 8); tag(259, 3, 1);
        tag(262, 3, 1); tag(273, 4, 122); tag(277, 3, 1); tag(278, 4, 1); tag(279, 4, 1);
        tiff[122] = 127;
        const auto path = fs::temp_directory_path() / ("mcp-imageio-" + to_string(getSystemTimeMicros()) + ".tiff");
        ofstream file(path, ios::binary);
        file.write(reinterpret_cast<const char*>(tiff.data()), tiff.size()); file.close();
        Pixels decoded;
        check("ImageIO-only TIFF loads through bounded MCP snapshot", mcp::internal::loadMcpImage(decoded, path) &&
            decoded.getWidth() == 1 && decoded.getHeight() == 1 && decoded.getChannels() == 4 && decoded.getData()[3] == 255);
        check("ImageIO snapshot credits retire", mcp::internal::workingMemory() == 0);
        fs::remove(path);
    }
#endif
#ifndef _WIN32
    if (!std::getenv("MCP_ADMISSION_SOCKET_FREE")) mcp::startHttpServer();
    const bool socketsAvailable = bool(mcp::internal::getHttpServer());
    if (socketsAvailable) {
    mcp::internal::getHttpServer()->set_keep_alive_timeout(30);
    vector<socket_t> sockets;
    for (int i = 0; i < 33; ++i) {
        if (i == 32) {
            auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
            while (mcp::internal::admittedConnections() < 32 && chrono::steady_clock::now() < deadline)
                this_thread::sleep_for(chrono::milliseconds(1));
            check("32 connections retain admission before body read", mcp::internal::admittedConnections() == 32);
        }
        auto sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(mcp::getHttpPort());
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        check("connection opens", connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        sockets.push_back(sock);
    }
    timeval timeout{5, 0};
    setsockopt(sockets.back(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    char byte;
    auto n = recv(sockets.back(), &byte, 1, 0);
    check("connection 33 is closed at admission", n == 0 || (n < 0 && errno == ECONNRESET));
    for (auto sock : sockets) { shutdown(sock, SHUT_RDWR); close(sock); }
    mcp::stopHttpServer();
    check("socket shutdown returns all admission credits", mcp::internal::admittedConnections() == 0 && mcp::internal::workingMemory() == 0);

    auto constrained = defaults;
    constrained.workingBytes = 8u * 1024 * 1024;
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(constrained));
    mcp::startHttpServer();
    {
        httplib::Client client("127.0.0.1", mcp::getHttpPort());
        client.set_read_timeout(3);
        auto reply = client.Post("/mcp", string(65536, ' '), "application/json");
        check("working admission rejects before a promise is queued", reply && reply->status == 503 &&
            reply->body.find("working memory") != string::npos && mcp::internal::getHttpChannel().size() == 0);
    }
    mcp::stopHttpServer();
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(defaults));
    mcp::startHttpServer();
    auto raw = [&](const string& text) {
        auto sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(mcp::getHttpPort());
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        timeval timeout{3, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        size_t sent = 0;
        while (sent < text.size()) {
            auto n = send(sock, text.data() + sent, text.size() - sent, 0);
            if (n <= 0) break;
            sent += size_t(n);
        }
        string response;
        char buffer[1024];
        for (;;) {
            auto n = recv(sock, buffer, sizeof(buffer), 0);
            if (n <= 0) break;
            response.append(buffer, size_t(n));
        }
        close(sock);
        return response;
    };
    for (int i = 0; i < 2; ++i) {
        check("MCP length limit rejects before body read", raw("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: 1048577\r\n\r\n").find("413") != string::npos);
        check("MCP header line is bounded while reading", raw("GET / HTTP/1.1\r\nX: " + string(16384, 'a')).find("431") != string::npos);
    }
    while (mcp::internal::admittedConnections() != 0) this_thread::yield();
    mcp::Tool deferred;
    deferred.name = "admission_reply";
    deferred.description = "Deferred admission reply";
    deferred.handler = [](const json&) -> json {
        mcp::deferToolResultUntilAfterFrame([] { return json{{"status", "ok"}}; });
        return nullptr;
    };
    mcp::internal::Server::instance().registerTool(deferred);
    bool activeSetting = !mcp::setAdmissionLimits(defaults);
    check("running listener keeps its settings", activeSetting);
    auto parsedReply = async(launch::async, [deepRequest] {
        httplib::Client client("127.0.0.1", mcp::getHttpPort());
        client.set_read_timeout(5);
        auto response = client.Post("/mcp", deepRequest, "application/json");
        return response && response->status == 503 && !response->get_header_value("X-MCP-Reason").empty();
    });
    auto parseDeadline = chrono::steady_clock::now() + chrono::seconds(3);
    while (mcp::internal::getHttpChannel().size() == 0 && chrono::steady_clock::now() < parseDeadline)
        this_thread::sleep_for(chrono::milliseconds(1));
    mcp::processHttpQueue();
    check("parse admission resolves the HTTP future with a reason", parsedReply.get());
    while (mcp::internal::admittedConnections() != 0) this_thread::yield();
    vector<future<bool>> replies;
    for (int i = 0; i < 32; ++i) {
        replies.push_back(async(launch::async, [] {
            httplib::Client client("127.0.0.1", mcp::getHttpPort());
            client.set_read_timeout(5);
            auto response = client.Post("/mcp", R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"admission_reply","arguments":{}}})", "application/json");
            return bool(response) && response->status == 200;
        }));
        // Admit each connection before opening the next: a simultaneous burst
        // can fill the OS listen backlog and delay SYN retries past the wait.
        auto acceptedDeadline = chrono::steady_clock::now() + chrono::seconds(3);
        while (mcp::internal::admittedConnections() < size_t(i + 1) && chrono::steady_clock::now() < acceptedDeadline)
            this_thread::sleep_for(chrono::milliseconds(1));
    }
    auto deadline = chrono::steady_clock::now() + chrono::seconds(3);
    while ((mcp::internal::getHttpChannel().size() < 4 || mcp::internal::admittedConnections() < 32) && chrono::steady_clock::now() < deadline)
        this_thread::sleep_for(chrono::milliseconds(1));
    check("four workers retain four admitted promises", mcp::internal::getHttpChannel().size() == 4);
    mcp::processHttpQueue();
    {
        lock_guard<mutex> lock(mcp::internal::deferredResponsesMutex());
        check("deferred replies retain admission", mcp::internal::deferredResponses().size() == 4 && mcp::internal::admittedConnections() == 32);
    }
    mcp::stopHttpServer();
    int answered = 0;
    for (auto& reply : replies) {
        check("shutdown future is resolved", reply.wait_for(chrono::seconds(1)) == future_status::ready);
        if (reply.get()) ++answered;
    }
    // Queued socket work can be closed when the listener stops; all client
    // futures must still finish and all four admitted promises must reply.
    check("shutdown answers admitted promises", answered >= 4);
    check("request shutdown returns all admission credits", mcp::internal::admittedConnections() == 0 && mcp::internal::workingMemory() == 0);
    atomic<bool> entered{false}, stopped{false}, produced{false};
    mcp::tool("admission_pending", "Pending admission reply").bind([&]() -> json {
        entered = true;
        while (!stopped) this_thread::yield();
        mcp::deferToolResultUntilAfterFrame([&] { produced = true; return json::object(); });
        return nullptr;
    });
    mcp::startHttpServer();
    auto pendingReply = async(launch::async, [] {
        httplib::Client client("127.0.0.1", mcp::getHttpPort());
        client.set_read_timeout(5);
        auto reply = client.Post("/mcp", R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"admission_pending","arguments":{}}})", "application/json");
        return reply && reply->status == 200 && reply->body.find("error") != string::npos;
    });
    thread watchdog([&] {
        for (int i = 0; i < 500 && !stopped; ++i) this_thread::sleep_for(chrono::milliseconds(10));
        if (!stopped) { printf("FAIL: pending shutdown timed out\n"); fflush(stdout); std::_Exit(2); }
    });
    thread stopper([&] {
        while (!entered) this_thread::yield();
        mcp::stopHttpServer();
        stopped = true;
    });
    deadline = chrono::steady_clock::now() + chrono::seconds(3);
    while (mcp::internal::getHttpChannel().size() == 0 && chrono::steady_clock::now() < deadline)
        this_thread::sleep_for(chrono::milliseconds(1));
    mcp::processHttpQueue();
    stopper.join();
    watchdog.join();
    check("shutdown answers an executing request", pendingReply.get());
    mcp::internal::drainDeferredResponses();
    check("late deferred response is discarded at shutdown", !produced && !mcp::internal::hasDeferredResponses() && mcp::internal::workingMemory() == 0);

    auto replyLimited = defaults;
    const auto toolList = mcp::internal::Server::instance().processMessage(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
    replyLimited.replyBytes = max<size_t>(4096, toolList.size() + 1024);
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(replyLimited));
    mcp::startHttpServer();
    auto limitedReply = async(launch::async, [idBytes = replyLimited.replyBytes + 1] {
        httplib::Client client("127.0.0.1", mcp::getHttpPort());
        client.set_read_timeout(5);
        const json request{{"jsonrpc", "2.0"}, {"id", string(idBytes, 'a')}, {"method", "tools/call"},
            {"params", {{"name", "admission_reply"}, {"arguments", json::object()}}}};
        auto reply = client.Post("/mcp", request.dump(), "application/json");
        return reply && reply->status == 503 && reply->body.empty() &&
            reply->get_header_value("X-MCP-Reason").find("limit") != string::npos;
    });
    deadline = chrono::steady_clock::now() + chrono::seconds(3);
    while (mcp::internal::getHttpChannel().size() == 0 && chrono::steady_clock::now() < deadline)
        this_thread::sleep_for(chrono::milliseconds(1));
    check("limited reply request is admitted", mcp::internal::getHttpChannel().size() == 1);
    mcp::processHttpQueue();
    mcp::stopHttpServer();
    check("shutdown resolves a promise when its error reply cannot fit", limitedReply.get());
    check("limited shutdown returns credits", mcp::internal::workingMemory() == 0 && mcp::internal::admittedConnections() == 0);
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(defaults));
    mcp::tool("admission_closed", "Closed admission reply").bind([]() -> json {
        mcp::internal::getHttpChannel().close();
        mcp::deferToolResultUntilAfterFrame([] { return json::object(); });
        return nullptr;
    });
    mcp::internal::resetHttpChannel();
    auto promise = make_shared<std::promise<mcp::internal::ReplyThunk>>();
    auto closedReply = promise->get_future();
    mcp::internal::getHttpChannel().send(mcp::McpRequest{
        R"({"jsonrpc":"2.0","id":91,"method":"tools/call","params":{"name":"admission_closed","arguments":{}}})", promise});
    mcp::processHttpQueue();
    const bool completed = closedReply.wait_for(chrono::seconds(0)) == future_status::ready;
    check("closed channel resolves a late response", completed);
    if (completed) {
        auto response = json::parse(closedReply.get()());
        check("shutdown reply preserves request id", response["id"] == 91 && response["result"].value("isError", false));
    } else mcp::stopHttpServer();
    } else {
        printf("SKIP: live MCP sockets unavailable or MCP_ADMISSION_SOCKET_FREE set; running socket-free regressions\n");
    }
#endif
    const uint64_t expected[] = {
#ifndef _WIN32
        socketsAvailable ? 2u : 0u, socketsAvailable ? 2u : 0u, socketsAvailable ? 1u : 0u,
#else
        0, 0, 0,
#endif
        3, 3, 2,
#ifndef _WIN32
        socketsAvailable ? 4u : 3u,
#else
        3,
#endif
#ifndef _WIN32
        socketsAvailable ? 6u : 4u
#else
        4
#endif
    };
    for (size_t i = 0; i < storeCount; ++i) {
        printf("store %s: %llu rejections, %d warnings\n", stores[i],
            static_cast<unsigned long long>(mcp::internal::admissionRejections(static_cast<AdmissionStore>(i))), warnings[i].load());
        check("exact cumulative rejection counter", mcp::internal::admissionRejections(static_cast<AdmissionStore>(i)) == expected[i]);
        check("exactly one Warning per rejecting store", warnings[i] == (expected[i] ? 1 : 0));
    }

    const string nestedIdRequest = R"({"jsonrpc":"2.0","id":)" +
        string(100000, '[') + "0" + string(100000, ']') + R"(,"method":"initialize"})";
    {
        const auto reply = json::parse(mcp::internal::Server::instance().processMessage(nestedIdRequest));
        check("nested id exceeds request depth limit", reply.is_object() &&
            reply.value("jsonrpc", "") == "2.0" && reply["id"].is_null() &&
            reply.contains("error") && reply["error"].value("code", 0) == -32000);
        const auto nextReply = json::parse(mcp::internal::Server::instance().processMessage(
            R"({"jsonrpc":"2.0","id":92,"method":"initialize"})"));
        check("request processing continues after depth rejection", nextReply["id"] == 92 && nextReply.contains("result"));
        check("nested id rejection releases request credits", mcp::internal::workingMemory() == 0);
    }

    // Model the same connection/request scope boundary installed in httplib.
    // These checks also run when a sandbox prohibits bind().
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(defaults));
    {
        auto connection = make_shared<mcp::internal::WorkBudget>(true);
        mcp::internal::WorkScope connectionScope(connection);
        connection->reserve(defaults.bodyBytes * 4 + defaults.headerBytes * 16);
        const auto base = mcp::internal::workingMemory();
        const string body = R"({"jsonrpc":"2.0","id":7,"method":"initialize","padding":")" +
            string(300000, 'a') + R"("})";
        for (int i = 0; i < 7; ++i) {
            {
                mcp::internal::WorkScope requestScope(make_shared<mcp::internal::WorkBudget>(true));
                mcp::internal::reserveWork(body.size() * 64);
                auto reply = json::parse(mcp::internal::Server::instance().processMessage(body));
                check("keep-alive model accepts sequential 300059-byte request", reply.contains("result") && reply["id"] == 7);
            }
            check("keep-alive model retires request credits and retains ingress", mcp::internal::workingMemory() == base);
        }
    }
    check("connection model returns all credits", mcp::internal::workingMemory() == 0);
    auto perReply = defaults;
    perReply.replyBytes = 2048;
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(perReply));
    {
        mcp::internal::WorkScope connectionScope(make_shared<mcp::internal::WorkBudget>(true));
        Pixels pixel;
        pixel.allocate(1, 1, 4);
        for (int i = 0; i < 7; ++i) {
            {
                mcp::internal::WorkScope requestScope(make_shared<mcp::internal::WorkBudget>(true));
                const auto reply = mcp::internal::analyzeImage(pixel,
                    {{"ops", json::array({{{"op", "pixel"}, {"x", 0}, {"y", 0}}})}}, "sRGB");
                check("reply allowance resets per modeled request", reply.contains("results"));
            }
            check("modeled reply credits retire", mcp::internal::workingMemory() == 0);
        }
        bool full = false;
        try {
            mcp::internal::WorkScope requestScope(make_shared<mcp::internal::WorkBudget>(true));
            mcp::internal::reserveReply(2049);
        } catch (const mcp::internal::AdmissionError&) { full = true; }
        check("each request still enforces its reply limit", full);
    }
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(defaults));
    {
        auto request = make_shared<mcp::internal::WorkBudget>(true);
        mcp::internal::ReplyThunk worker;
        {
            mcp::internal::WorkScope scope(request);
            mcp::internal::reserveWork(4096);
            worker = [budget = request] {
                mcp::internal::WorkScope scope(budget);
                return string("{}");
            };
        }
        request.reset();
        check("worker stage retains request credits after main scope", mcp::internal::workingMemory() == 4096);
        check("retained worker stage completes", worker() == "{}");
        worker = {};
        check("last worker owner retires request credits", mcp::internal::workingMemory() == 0);
    }
    auto terminal = [&](const char* name, int mode, bool tiny) {
        auto limits = defaults;
        if (tiny) limits.replyBytes = 32;
        check("admission limits configuration succeeds", mcp::setAdmissionLimits(limits));
        auto promise = make_shared<std::promise<mcp::internal::ReplyThunk>>();
        auto future = promise->get_future();
        int owner;
        mcp::internal::DeferredResponse d;
        d.budget = make_shared<mcp::internal::WorkBudget>(true);
        d.id = 77;
        d.response = promise;
        d.owner = &owner;
        d.makeEnvelope = [mode]() -> mcp::internal::ReplyThunk {
            if (mode == 2) throw mcp::internal::AdmissionError();
            throw runtime_error("producer failed");
        };
        if (mode == 0) {
            d.target = &owner;
            d.deadline = chrono::steady_clock::time_point::min();
        }
        mcp::internal::deferredResponses().push_back(std::move(d));
        bool threw = false;
        try {
            if (mode == 1) mcp::internal::removeRegistrationsOwnedBy(&owner);
            else mcp::internal::drainDeferredResponses();
        } catch (...) { threw = true; }
        const bool ready = future.wait_for(chrono::seconds(0)) == future_status::ready;
        check(name, !threw && ready && !mcp::internal::hasDeferredResponses());
        if (ready) {
            bool quota = false;
            string reply;
            try { reply = future.get()(); } catch (const mcp::internal::AdmissionError&) { quota = true; }
            if (tiny) check("unencodable terminal error publishes HTTP 503 thunk", quota && reply.empty());
            else {
                const auto envelope = json::parse(reply, nullptr, false);
                check("terminal error is a valid id-preserving JSON-RPC envelope", !quota && envelope.is_object() &&
                    envelope.value("jsonrpc", "") == "2.0" && envelope["id"] == 77 && envelope["result"].value("isError", false));
            }
        }
        check("terminal completion retires stage credits", mcp::internal::workingMemory() == 0);
    };
    terminal("expired tiny reply future is ready", 0, true);
    terminal("owner cleanup tiny reply future is ready", 1, true);
    terminal("deferred admission tiny reply future is ready", 2, true);
    terminal("deferred exception tiny reply future is ready", 3, true);
    terminal("deferred admission produces correlated error", 2, false);
    terminal("deferred exception produces correlated error", 3, false);
#ifndef __EMSCRIPTEN__
    for (int mode = 0; mode < 4; ++mode) {
        auto limits = defaults;
        if (mode == 1) limits.replyBytes = 32;
        check("admission limits configuration succeeds", mcp::setAdmissionLimits(limits));
        mcp::internal::resetHttpChannel();
        auto promise = make_shared<std::promise<mcp::internal::ReplyThunk>>();
        auto future = promise->get_future().share();
        string body;
        if (mode == 0 || mode == 3) body = nestedIdRequest;
        else if (mode == 1) body = R"({"id":")" + string(4096, 'a') + R"("})";
        else body = R"({"id":91})";
        if (mode == 3) mcp::internal::getHttpChannel().close();
        mcp::internal::enqueueHttpRequest(mcp::McpRequest{body, promise, make_shared<mcp::internal::WorkBudget>(true)}, future);
        mcp::stopHttpServer();
        const bool ready = future.wait_for(chrono::seconds(0)) == future_status::ready;
        check(mode == 0 ? "queued nested id resolves when server stops" :
            "queued/closed shutdown completes even with depth or reply rejection", ready);
        if (ready) {
            bool quota = false;
            string reply;
            try { reply = future.get()(); } catch (const mcp::internal::AdmissionError&) { quota = true; }
            if (mode != 2) check(mode == 0 ? "queued nested id returns bounded shutdown error" :
                "shutdown rejection has a bounded HTTP 503 thunk", quota && reply.empty());
            else check("bounded shutdown envelope retains id", json::parse(reply)["id"] == 91 && reply.size() <= limits.replyBytes);
        }
        check("shutdown model releases credits", mcp::internal::workingMemory() == 0);
    }
#endif
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(defaults));
#ifndef _WIN32
    if (socketsAvailable) {
        // Real keep-alive requests use one httplib client socket. Capture all
        // response statuses; main services requests while the client runs.
        auto live = [&](bool smallReplies) {
            auto limits = defaults;
            if (smallReplies) limits.replyBytes = 2048;
            check("admission limits configuration succeeds", mcp::setAdmissionLimits(limits));
            mcp::tool("admission_pixel", "One pixel").bind([&]() -> json {
                Pixels pixel;
                pixel.allocate(1, 1, 4);
                return mcp::internal::analyzeImage(pixel,
                    {{"ops", json::array({{{"op", "pixel"}, {"x", 0}, {"y", 0}}})}}, "sRGB");
            });
            mcp::startHttpServer();
            atomic<bool> done{false};
            vector<bool> results;
            vector<size_t> connections;
            auto client = async(launch::async, [&] {
                httplib::Client c("127.0.0.1", mcp::getHttpPort());
                c.set_keep_alive(true);
                c.set_read_timeout(5);
                for (int i = 0; i < 7; ++i) {
                    const string body = smallReplies
                        ? R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"admission_pixel","arguments":{}}})"
                        : R"({"jsonrpc":"2.0","id":7,"method":"initialize","padding":")" + string(300000, 'a') + R"("})";
                    auto response = c.Post("/mcp", body, "application/json");
                    const auto envelope = response ? json::parse(response->body, nullptr, false) : json();
                    const bool pixelOk = !smallReplies || (envelope.contains("result") &&
                        envelope["result"].contains("content") &&
                        json::parse(envelope["result"]["content"][0].value("text", "null"), nullptr, false).contains("results"));
                    results.push_back(response && response->status == 200 && envelope.contains("result") && pixelOk);
                    connections.push_back(mcp::internal::admittedConnections());
                }
                done = true;
            });
            while (!done) { mcp::processHttpQueue(); this_thread::sleep_for(chrono::milliseconds(1)); }
            client.get();
            for (bool ok : results) check(smallReplies ? "live keep-alive reply budget resets" : "live keep-alive accepts sequential large requests", ok);
            check("live keep-alive retains exactly one socket admission", all_of(connections.begin(), connections.end(), [](size_t n) { return n == 1; }));
            mcp::stopHttpServer();
            check("live keep-alive releases all credits", mcp::internal::workingMemory() == 0 && mcp::internal::admittedConnections() == 0);
        };
        live(false);
        live(true);

        mcp::tool("admission_mainstage", "Main-stage limit").bind([]() -> json {
            mcp::deferToolResultTwoStage([]() -> std::function<json()> {
                throw mcp::internal::AdmissionError();
            });
            return nullptr;
        });
        for (bool tiny : {false, true}) {
            auto limits = defaults;
            limits.replyBytes = tiny ? 32 : 2048;
            check("admission limits configuration succeeds", mcp::setAdmissionLimits(limits));
            mcp::startHttpServer();
            atomic<bool> done{false};
            auto client = async(launch::async, [&] {
                httplib::Client c("127.0.0.1", mcp::getHttpPort());
                c.set_read_timeout(5);
                auto response = c.Post("/mcp", R"({"jsonrpc":"2.0","id":77,"method":"tools/call","params":{"name":"admission_mainstage","arguments":{}}})", "application/json");
                done = true;
                return response;
            });
            while (!done) {
                mcp::processHttpQueue();
                mcp::internal::drainDeferredResponses();
                this_thread::sleep_for(chrono::milliseconds(1));
            }
            auto response = client.get();
            if (tiny) check("live main-stage admission uses HTTP 503 if error cannot fit", response &&
                response->status == 503 && response->body.empty() && !response->get_header_value("X-MCP-Reason").empty());
            else {
                const auto envelope = response ? json::parse(response->body, nullptr, false) : json();
                check("live main-stage admission returns correlated JSON-RPC tool error", response &&
                    response->status == 200 && envelope.value("jsonrpc", "") == "2.0" &&
                    envelope["id"] == 77 && envelope["result"].value("isError", false));
            }
            mcp::stopHttpServer();
            check("live deferred failure releases all credits", mcp::internal::workingMemory() == 0 && mcp::internal::admittedConnections() == 0);
        }
    }
#endif
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(defaults));
    for (size_t i = 0; i < storeCount; ++i)
        printf("final store %s: %llu rejections, %d warnings\n", stores[i],
            static_cast<unsigned long long>(mcp::internal::admissionRejections(static_cast<AdmissionStore>(i))), warnings[i].load());
    listener.disconnect();
    printf("mcpAdmission: %d failures\n", failures);
    return failures ? 1 : 0;
}
