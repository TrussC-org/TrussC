// Socket-free fixture: the production adapter is inserted by test_mcp_ingress_stream.py.
#include "impl/httplib.h"
#include <cassert>
#include <cstdio>

enum class AdmissionStore { Headers };
size_t rejections = 0;
void admissionRejected(AdmissionStore, size_t, size_t, const char*) { ++rejections; }

// PRODUCTION_INGRESS_STREAM

struct Input : httplib::Stream {
    std::string data, output;
    size_t offset = 0, loaded = 0;
    mutable size_t readWaits = 0, writeWaits = 0, peerChecks = 0;
    std::vector<time_t> deadlines;
    time_t readSeconds = 0;
    bool interrupted = false;
    explicit Input(std::string text) : data(std::move(text)) {}
    bool is_readable() const override { return offset < loaded; }
    bool wait_readable() const override { ++readWaits; return offset < data.size(); }
    bool wait_writable() const override { ++writeWaits; return true; }
    bool is_peer_alive() const override { ++peerChecks; return false; }
    time_t duration() const override { return 37; }
    void set_max_timeout(time_t msec) override { deadlines.push_back(msec); }
    void set_read_timeout(time_t sec, time_t) override { readSeconds = sec; }
    void set_read_interrupt(std::function<bool()> callback) override { interrupted = callback(); }
    const char* buffered_data(size_t& size) const override {
        size = loaded - offset;
        return size ? data.data() + offset : nullptr;
    }
    ssize_t read(char* out, size_t size) override {
        // Model SocketStream's fixed read-ahead, including pipelined requests.
        if (offset == loaded) loaded = (std::min)(data.size(), offset + 4096);
        size = (std::min)(size, loaded - offset);
        std::memcpy(out, data.data() + offset, size);
        offset += size;
        return static_cast<ssize_t>(size);
    }
    ssize_t write(const char* ptr, size_t size) override {
        output.append(ptr, size);
        return static_cast<ssize_t>(size);
    }
    void get_remote_ip_and_port(std::string&, int&) const override {}
    void get_local_ip_and_port(std::string&, int&) const override {}
    socket_t socket() const override { return INVALID_SOCKET; }
};

struct Server : httplib::Server {
    McpIngressStream* active = nullptr;
    bool closed = false, refused = false;
    uint64_t owed = 0;
    int handled = 0;
    Server() {
        Get("/", [&](const httplib::Request&, httplib::Response& res) {
            ++handled;
            res.set_content("ok", "text/plain");
        });
        Post("/", [&](const httplib::Request&, httplib::Response& res, const httplib::ContentReader& reader) {
            if (!reader([](const char*, size_t) { return true; })) {
                if (active->overflowed()) res.status = 431;
                res.set_header("Connection", "close");
                return;
            }
            ++handled;
            res.set_content("ok", "text/plain");
        });
        set_logger([&](const httplib::Request& req, const httplib::Response&) { active->response(req); });
    }
    bool process(Input& input) {
        McpIngressStream ingress(input);
        active = &ingress;
        closed = false;
        const auto ok = process_request(ingress, "127.0.0.1", 1, "127.0.0.1", 1,
            false, closed, [&](httplib::Request& req) { ingress.body(req); });
        refused = ingress.refused();
        owed = ingress.refusalRemainder();
        if (ingress.overflowed()) ingress.write("", 0);
        closed = closed || refused;
        active = nullptr;
        return ok;
    }
};

std::string fields(size_t count, const std::string& line = "X-Test: a\r\n") {
    std::string result;
    while (count--) result += line;
    return result;
}

int main() {
    {
        Input input("abc");
        McpIngressStream ingress(input);
        assert(!ingress.is_readable());
        assert(ingress.wait_readable() && ingress.wait_writable());
        assert(!ingress.is_peer_alive() && input.peerChecks == 1 && input.writeWaits == 1);
        assert(ingress.duration() == 37);
        ingress.set_read_timeout(3);
        ingress.set_read_interrupt([] { return true; });
        ingress.set_max_timeout(123);
        assert(input.readSeconds == 3 && input.interrupted && input.deadlines.back() == 123);
    }
    const std::string get = "GET / HTTP/1.1\r\nHost: localhost\r\n";
    const std::string post = "POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n";
    auto status = [&](const std::string& wire, int expected, size_t rejected) {
        Server server;
        Input input(wire);
        const auto before = rejections;
        server.process(input);
        assert(input.output.find("HTTP/1.1 " + std::to_string(expected) + " ") == 0);
        assert(rejections - before == rejected);
        assert((expected < 400) == !server.refused);
        if (expected >= 400) assert(server.closed);
    };
    status(get + fields(99) + "\r\n", 200, 0);
    status("\r\n" + get + fields(99) + "\r\n", 200, 0);
    status("\r\nGET /" + std::string(8192, 'a') + " HTTP/1.1\r\nHost: localhost\r\n\r\n", 414, 0);
    status(get + fields(100) + "\r\n", 431, 1);
    status(post + fields(100) + "\r\n", 200, 0);
    status(post + fields(100) + "X-Test: a", 431, 1);
    status(get + "\rX-Test: a\r\n\r\n", 400, 0);
    status(post + "\rX-Test: a\r\n\r\n", 400, 0);
    status(get + "X-Long: " + std::string(8192, 'a') + "\r\n\r\n", 400, 0);
    status(post + "X-Long: " + std::string(8192, 'a') + "\r\n\r\n", 400, 0);
    status(post + "X-Padded:" + std::string(65536, ' ') + "\r\n\r\n", 400, 0);
    status("POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\nfffffffffffffffff\r\n", 400, 0);
    status(get + "Range: bytes=invalid\r\n\r\n", 416, 0);
    // Exactly 65536 raw section bytes includes the final CRLF; keep individual lines valid.
    const std::string host = "Host: localhost\r\n";
    const std::string padded = fields(8, "X: " + std::string(7995, 'a') + "\r\n");
    const auto fill = 65536 - host.size() - padded.size() - 2 - 5;
    const auto headers = host + padded + "X: " + std::string(fill, 'a') + "\r\n";
    status("GET / HTTP/1.1\r\n" + headers + "\r\n", 200, 0);
    status("GET / HTTP/1.1\r\n" + headers + " \r\n", 431, 1);
    const auto trailers = padded + "X: " + std::string(fill + host.size(), 'a') + "\r\n";
    status(post + trailers + "\r\n", 200, 0);
    status(post + trailers + " \r\n", 431, 1);
    {
        Server server;
        Input input(get + "\r\n" + get + "Connection: close\r\n\r\n");
        assert(server.process(input) && !server.closed);
        assert(input.is_readable()); // Next request remains in the connection's read-ahead.
        assert(server.process(input) && server.closed && server.handled == 2);
        assert((input.deadlines == std::vector<time_t>{5000, 0, 5000, 0}));
    }
    {
        Server server;
        Input input("POST / HTTP/1.1\r\nContent-Length: 5000\r\n\r\n" + std::string(5000, 'a'));
        server.process(input); // Missing Host is rejected before setup_request.
        assert(server.closed && server.refused && server.handled == 0);
        assert(input.output.find("HTTP/1.1 400 ") == 0);
        assert(server.owed == input.data.size() - input.loaded);
    }
    {
        Server server;
        Input input(get + "Content-Length: invalid\r\n\r\n");
        server.process(input);
        assert(server.closed && server.refused && server.handled == 0);
        assert(server.owed == (std::numeric_limits<uint64_t>::max)());
    }
    std::puts("MCP ingress: deadlines, readiness, pipelining, refusal accounting and field/byte boundaries passed");
}
