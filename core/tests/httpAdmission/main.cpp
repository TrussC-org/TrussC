#include "../../include/impl/httplib.h"
#include <cstdio>

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
struct Input : httplib::Stream {
    std::string data;
    size_t readBytes = 0;
    std::string output;
    explicit Input(std::string text) : data(std::move(text)) {}
    bool is_readable() const override { return true; }
    bool is_writable() const override { return true; }
    ssize_t read(char* out, size_t n) override {
        n = std::min(n, data.size() - readBytes);
        std::memcpy(out, data.data() + readBytes, n);
        readBytes += n;
        return static_cast<ssize_t>(n);
    }
    ssize_t write(const char* p, size_t n) override { output.append(p, n); return static_cast<ssize_t>(n); }
    void get_remote_ip_and_port(std::string&, int&) const override {}
    void get_local_ip_and_port(std::string&, int&) const override {}
    socket_t socket() const override { return INVALID_SOCKET; }
};
struct Server : httplib::Server {
    void process(Input& input) {
        bool closed = false;
        process_request(input, "127.0.0.1", 1, "127.0.0.1", 1, true, closed, nullptr);
    }
    size_t bodyRejections() const { return payload_rejections_.load(); }
    size_t headerRejections() const { return header_rejections_.load(); }
    size_t descriptorRejections() const { return descriptor_rejections_.load(); }
};

void body(const char* name, std::string data, httplib::Headers headers, bool noRead = false, size_t limit = 32) {
    Input input(std::move(data));
    httplib::Request req;
    req.headers = std::move(headers);
    int status = 0;
    size_t received = 0;
    bool accepted = httplib::detail::read_content(input, req, limit, status, nullptr,
        [&](const char*, size_t n, uint64_t, uint64_t) { received += n; return true; }, true);
    check(name, !accepted && status == 413 && received <= limit && (!noRead || input.readBytes == 0));
}
}
int main() {
    check("default HTTP payload is 64 MiB", CPPHTTPLIB_PAYLOAD_MAX_LENGTH == 64u * 1024 * 1024);
    body("content length is rejected before reading", std::string(33, 'a'), {{"Content-Length", "33"}}, true);
    body("chunked bytes are counted cumulatively", "10\r\n" + std::string(16, 'a') + "\r\n11\r\n" + std::string(17, 'b') + "\r\n0\r\n\r\n", {{"Transfer-Encoding", "chunked"}});
    body("body without length is counted cumulatively", std::string(33, 'a'), {});

#ifdef CPPHTTPLIB_ZLIB_SUPPORT
    std::string compressed;
    httplib::detail::gzip_compressor gzip;
    gzip.compress(std::string(4096, 'a').data(), 4096, true,
        [&](const char* p, size_t n) { compressed.append(p, n); return true; });
    body("decoded compressed bytes are bounded", compressed, {{"Content-Length", std::to_string(compressed.size())}, {"Content-Encoding", "gzip"}}, false, 64);
#else
    std::puts("SKIP: compressed body check requires zlib");
#endif
    {
        Input input(std::string(32768, 'a'));
        httplib::Headers headers;
        check("unterminated header line stops while reading", !httplib::detail::read_headers(input, headers) && input.readBytes <= 8193 && headers.empty());
    }
    {
        std::string data;
        for (int i = 0; i < 1000; ++i) data += "X: a\r\n";
        data += "\r\n";
        Input input(data);
        httplib::Headers headers;
        check("many headers stop before insertion", !httplib::detail::read_headers(input, headers) && headers.size() <= 128 && input.readBytes < data.size());
    }
    {
        std::string data;
        for (int i = 0; i < 20; ++i) data += "X: " + std::string(4090, 'a') + "\r\n";
        data += "\r\n";
        Input input(data);
        httplib::Headers headers;
        check("aggregate header bytes stop before insertion", !httplib::detail::read_headers(input, headers) && input.readBytes <= 65537);
    }

    {
        std::ostringstream diagnostics;
        auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
        Server server;
        int handled = 0;
        server.Post("/", [&](const httplib::Request&, httplib::Response& res) { ++handled; res.set_content("ok", "text/plain"); });
        server.set_payload_max_length(128);
        Input input("POST / HTTP/1.1\r\nContent-Length: 129\r\n\r\n");
        server.process(input);
        check("oversize content length returns 413 without body read", input.output.find("413") != std::string::npos && handled == 0 && server.bodyRejections() == 1);
        Input multipart("POST / HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=a\r\nTransfer-Encoding: chunked\r\n\r\n100\r\n--a\r\nContent-Disposition: form-data; name=\"x\"\r\n\r\n" + std::string(256, 'x'));
        server.process(multipart);
        check("multipart aggregate is bounded before dispatch", multipart.output.find("413") != std::string::npos && handled == 0 && server.bodyRejections() == 2);
        server.set_payload_max_length(65536);
        Input trailer("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX: " + std::string(10000, 'a'));
        server.process(trailer);
        check("unterminated trailer returns 431 while reading", trailer.output.find("431") != std::string::npos && trailer.readBytes < 8400 && handled == 0);
        Input requestLine("GET /" + std::string(10000, 'a'));
        server.process(requestLine);
        check("unterminated request line returns 431 while reading", requestLine.output.find("431") != std::string::npos && requestLine.readBytes <= 8193);
        std::string headers = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n";
        for (int i = 0; i < 126; ++i) headers += "X: a\r\n";
        headers += "\r\n0\r\nY: b\r\nZ: c\r\n\r\n";
        Input shared(headers);
        server.process(shared);
        check("headers and trailers share the field allowance", shared.output.find("431") != std::string::npos && handled == 0);
        Input chunkLine("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n" + std::string(10000, 'f'));
        server.process(chunkLine);
        check("unterminated chunk line returns 431 while reading", chunkLine.output.find("431") != std::string::npos && chunkLine.readBytes < 8400);
        Input chunkSize("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\nffffffffffffffff\r\n");
        server.process(chunkSize);
        check("unrepresentable chunk size returns 413 and is counted", chunkSize.output.find("413") != std::string::npos && server.bodyRejections() == 3);
        server.set_header_limits(32, 64, 2);
        Input configured("GET / HTTP/1.1\r\nX: " + std::string(33, 'a'));
        server.process(configured);
        check("configured line limit is enforced while reading", configured.output.find("431") != std::string::npos && configured.readBytes < 55);
        check("exact header rejection counter", server.headerRejections() == 5);
#if !defined(_WIN32) && !defined(CPPHTTPLIB_USE_POLL)
        struct DescriptorInput : Input {
            using Input::Input;
            socket_t socket() const override { return FD_SETSIZE; }
        };
        for (int i = 0; i < 2; ++i) {
            DescriptorInput descriptor("GET / HTTP/1.1\r\n\r\n");
            server.process(descriptor);
            check("invalid descriptor is rejected and counted", descriptor.output.find("500") != std::string::npos && server.descriptorRejections() == size_t(i + 1));
        }
#endif
        std::cerr.rdbuf(previous);
        const auto lines = diagnostics.str();
        auto once = [&](const char* store) {
            const std::string prefix = std::string("Warning: HTTP ") + store + " capacity ";
            const auto first = lines.find(prefix);
            return first != std::string::npos && lines.find(prefix, first + 1) == std::string::npos;
        };
        check("one HTTP Warning per rejecting store", once("body") && once("headers")
#if !defined(_WIN32) && !defined(CPPHTTPLIB_USE_POLL)
              && once("descriptors")
#endif
        );
    }
    {
        struct Incomplete : Input {
            using Input::Input;
            ssize_t read(char* out, size_t n) override {
                return readBytes == data.size() ? -1 : Input::read(out, n);
            }
        } input("{}");
        httplib::Request request;
        int status = 0;
        bool accepted = httplib::detail::read_content(input, request, 32, status, nullptr,
            [](const char*, size_t, uint64_t, uint64_t) { return true; }, false);
        check("body read failure rejects an incomplete request", !accepted && status == 400);
    }
    std::printf("httpAdmission: %d failures\n", failures);
    return failures ? 1 : 0;
}
