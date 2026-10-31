#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../../include/impl/httplib.h"
#include "../../../include/tc/utils/tcAnalyzeImage.h"
#include <cstdio>
using namespace tc;
using namespace std;
namespace {
int failures = 0;
void check(const char* name, bool ok) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
} // namespace
TC_CORE_TEST_MAIN() {
    std::atomic<int> bodyWarnings{0}, headerWarnings{0};
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level != LogLevel::Warning) return;
        if (event.message.find("[MCP] body capacity ") == 0) ++bodyWarnings;
        if (event.message.find("[MCP] headers capacity ") == 0) ++headerWarnings;
    });
    const auto defaults = mcp::getAdmissionLimits();
    check("ingress defaults", defaults.bodyBytes == 64u * 1024 * 1024 && defaults.outstanding == 32 && defaults.workers == 4);
    auto invalid = defaults;
    invalid.bodyBytes = 0;
    check("zero configuration is rejected", !mcp::setAdmissionLimits(invalid) && mcp::getAdmissionLimits().bodyBytes == defaults.bodyBytes);
    auto& server = mcp::internal::Server::instance();
    check("control JSON above 1 MiB is accepted", json::parse(server.processMessage(
        string(1024 * 1024, ' ') + R"({"jsonrpc":"2.0","id":1,"method":"initialize"})")).contains("result"));
    check("oversize control JSON is rejected", json::parse(server.processMessage(string(defaults.bodyBytes + 1, ' '))).contains("error"));
    string nested(66, '['); nested += "0"; nested += string(66, ']');
    check("JSON nesting guard remains", json::parse(server.processMessage(nested)).contains("error"));
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
    check("socket shutdown returns all admission credits", mcp::internal::admittedConnections() == 0);

    check("limits restored", mcp::setAdmissionLimits(defaults));
    mcp::startHttpServer();
    bool connectionClosed = false;
    auto raw = [&](const string& text) {
        auto pending = std::async(std::launch::async, [&] {
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
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags = MSG_NOSIGNAL;
#endif
            auto n = send(sock, text.data() + sent, text.size() - sent, flags);
            if (n <= 0) break;
            sent += size_t(n);
        }
        string response;
        char buffer[1024];
        connectionClosed = false;
        for (;;) {
            auto n = recv(sock, buffer, sizeof(buffer), 0);
            if (n <= 0) { connectionClosed = n == 0 || errno == ECONNRESET; break; }
            response.append(buffer, size_t(n));
        }
        close(sock);
        return response;
        });
        while (pending.wait_for(chrono::milliseconds(1)) != std::future_status::ready) mcp::processHttpQueue();
        return pending.get();
    };
    for (int i = 0; i < 2; ++i) {
        check("MCP length limit rejects before body read", raw("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: " + to_string(defaults.bodyBytes + 1) + "\r\n\r\n").find("413") != string::npos);
    }
    check("long request line is rejected", raw("GET /" + string(8192, 'a') + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n").find("414") != string::npos);
    int toolCalls = 0;
    mcp::tool("metadata_probe", "verify refusal before dispatch").bind([&]() -> json {
        ++toolCalls;
        return {{"status", "ok"}};
    });
    const string toolBody = R"({"jsonrpc":"2.0","id":37,"method":"tools/call","params":{"name":"metadata_probe"}})";
    char chunkSize[32];
    std::snprintf(chunkSize, sizeof(chunkSize), "%zx", toolBody.size());
    const string toolPrefix = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n";
    const auto firstTrailer = raw(toolPrefix + chunkSize + "\r\n" + toolBody + "\r\n0\r\nX-Padded:" + string(65536, ' ') + "\r\n\r\n");
    check("first trailer overflow refuses before tool dispatch", toolCalls == 0 && connectionClosed &&
        firstTrailer.find("431") != string::npos && firstTrailer.find("HTTP/1.1", 1) == string::npos);
    const auto chunkExtension = raw(toolPrefix + chunkSize + ";" + string(1024 * 1024, 'a') + "\r\n" + toolBody + "\r\n0\r\n\r\n");
    check("1 MiB chunk extension refuses before tool dispatch", toolCalls == 0 && connectionClosed &&
        chunkExtension.find("431") != string::npos && chunkExtension.find("HTTP/1.1", 1) == string::npos);
    const auto acceptedTool = raw(toolPrefix + chunkSize + "\r\n" + toolBody + "\r\n0\r\n\r\n");
    check("metadata probe tool is callable after refusals", toolCalls == 1 && acceptedTool.find("200") != string::npos);
    string headers;
    for (int i = 0; i < 129; ++i) headers += "X-Test: a\r\n";
    check("header count is rejected", raw("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n" + headers + "\r\n").find("431") != string::npos);
    headers.clear();
    for (int i = 0; i < 9; ++i) headers += "X-Test: " + string(8000, 'a') + "\r\n";
    check("aggregate headers are rejected", raw("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n" + headers + "\r\n").find("431") != string::npos);
    headers.clear();
    for (int i = 0; i < 129; ++i) headers += "\rX-Test: a\r\n";
    check("noncanonical initial field names still count", raw("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n" + headers + "\r\n").find("431") != string::npos && connectionClosed);
    headers.clear();
    for (int i = 0; i < 10; ++i) headers += "X-Padded:" + string(7000, ' ') + "a\r\n";
    check("wire whitespace counts toward header bytes", raw("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n" + headers + "\r\n").find("431") != string::npos && connectionClosed);
    const string chunkPrefix = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n";
    headers.clear();
    for (int i = 0; i < 129; ++i) headers += "X-Trailer: a\r\n";
    check("trailer count is bounded before storage", raw(chunkPrefix + headers + "\r\n").find("431") != string::npos && connectionClosed);
    headers.clear();
    for (int i = 0; i < 10; ++i) headers += "X-Padded:" + string(7000, ' ') + "a\r\n";
    check("wire whitespace counts toward trailer bytes", raw(chunkPrefix + headers + "\r\n").find("431") != string::npos && connectionClosed);
    headers.clear();
    for (int i = 0; i < 126; ++i) headers += "X-Test: a\r\n";
    check("128 initial fields are accepted", raw("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n" + headers + "\r\n").find("200") != string::npos);
    headers.clear();
    for (int i = 0; i < 129; ++i) headers += "\rX-Trailer: a\r\n";
    check("noncanonical trailer names still count", raw(chunkPrefix + headers + "\r\n").find("431") != string::npos && connectionClosed);
    headers.clear();
    for (int i = 0; i < 128; ++i) headers += "X-Trailer: a\r\n";
    check("excess trailer is refused before its line completes", raw(chunkPrefix + headers + "X-Trailer: a").find("431") != string::npos && connectionClosed);
    const string validBody = R"({"jsonrpc":"2.0","id":1,"method":"initialize"})";
    // Multiple chunks, including one-byte data, exercise framing observation.
    const string validChunks = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n\r\n1\r\n" +
        validBody.substr(0, 1) + "\r\n2d\r\n" + validBody.substr(1) + "\r\n0\r\n";
    check("128 trailers and valid chunk framing are accepted", raw(validChunks + headers + "\r\n").find("200") != string::npos);
    const string following = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    const auto refused = raw("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: " + to_string(defaults.bodyBytes + 1) + "\r\n\r\n" + following);
    check("refusal with unread body closes without processing its bytes", connectionClosed && refused.find("413") != string::npos && refused.find("HTTP/1.1", 1) == string::npos);
    const auto forbidden = raw("POST /mcp HTTP/1.1\r\nHost: attacker.example\r\nContent-Type: application/json\r\nContent-Length: " + to_string(following.size()) + "\r\n\r\n" + following);
    check("host refusal with unread body closes", connectionClosed && forbidden.find("403") != string::npos && forbidden.find("HTTP/1.1", 1) == string::npos);
    mcp::stopHttpServer();
    auto ingressLimits = defaults;
    ingressLimits.bodyBytes = 64;
    check("small ingress limits configured", mcp::setAdmissionLimits(ingressLimits));
    mcp::startHttpServer();
    const string prefix = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n";
    check("chunked payload exceeding the limit is rejected",
        raw(prefix + "Transfer-Encoding: chunked\r\n\r\n41\r\n" + string(65, 'a') + "\r\n0\r\n\r\n").find("413") != string::npos);
    check("unregistered body routes preserve upstream 404",
        raw("PUT /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n").find("404") != string::npos);
    check("upstream invalid chunk status is preserved",
        raw(prefix + "Transfer-Encoding: chunked\r\n\r\nffffffffffffffff\r\n").find("400") != string::npos);
#ifdef CPPHTTPLIB_ZLIB_SUPPORT
    string compressed;
    httplib::detail::gzip_compressor gzip;
    gzip.compress(string(4096, 'a').data(), 4096, true,
        [&](const char* data, size_t count) { compressed.append(data, count); return true; });
    check("decoded bytes are checked before append", raw(prefix + "Content-Encoding: gzip\r\nContent-Length: " +
        to_string(compressed.size()) + "\r\n\r\n" + compressed).find("413") != string::npos);
#endif
    mcp::stopHttpServer();
    check("ingress limits restored", mcp::setAdmissionLimits(defaults));
    mcp::startHttpServer();
    while (mcp::internal::admittedConnections() != 0) this_thread::yield();
    mcp::stopHttpServer();
    check("header refusals counted", mcp::internal::admissionRejections(mcp::internal::AdmissionStore::Headers) == 9);
    check("one header warning across HTTP restarts", headerWarnings == 1);
    } else check("HTTP sockets are available", false);
#endif
    check("body rejections counted", mcp::internal::admissionRejections(mcp::internal::AdmissionStore::Body) > 0);
    check("one body warning across HTTP restarts", bodyWarnings == 1);
    mcp::internal::resetHttpChannel();
    auto responsePromise = make_shared<std::promise<mcp::internal::ReplyThunk>>();
    auto future = responsePromise->get_future();
    mcp::internal::getHttpChannel().send(mcp::McpRequest{
        R"({"jsonrpc":"2.0","id":77,"method":"initialize"})", responsePromise});
    mcp::stopHttpServer();
    check("shutdown answers queued request with its id", json::parse(future.get()())["id"] == 77);
    return failures ? 1 : 0;
}
