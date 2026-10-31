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
};
}
int main() {
    {
        Server server;
        int handled = 0;
        server.Post("/", [&](const httplib::Request&, httplib::Response& res) {
            ++handled;
            res.set_content("ok", "text/plain");
        });
        server.set_payload_max_length(32);
        Input oversized("POST / HTTP/1.1\r\nContent-Length: 33\r\n\r\n" + std::string(33, 'a'));
        server.process(oversized);
        check("upstream payload setting returns 413 before dispatch", oversized.output.find("413") != std::string::npos && handled == 0);
        Input boundary("POST / HTTP/1.1\r\nContent-Length: 32\r\n\r\n" + std::string(32, 'a'));
        server.process(boundary);
        check("exact payload allowance reaches handler", boundary.output.find("200 OK") != std::string::npos && handled == 1);
    }
    {
        httplib::Server server;
        server.set_read_timeout(0, 100000);
        server.set_write_timeout(0, 100000);
        server.set_keep_alive_max_count(3);
        server.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content("ok", "text/plain"); });
        const int port = server.bind_to_any_port("127.0.0.1");
        if (port <= 0) {
            std::puts("SKIP: live HTTP sockets unavailable");
        } else {
            std::thread listener([&] { server.listen_after_bind(); });
            server.wait_until_ready();
            httplib::Client client("127.0.0.1", port);
            client.set_keep_alive(true);
            for (int i = 0; i < 3; ++i) {
                auto response = client.Get("/");
                check("keep-alive request succeeds", response && response->status == 200);
                check("upstream keep-alive count closes at configured maximum", response &&
                    ((response->get_header_value("Connection") == "close") == (i == 2)));
            }
#ifndef _WIN32
            auto sock = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            const bool connected = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
            timeval timeout{2, 0};
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            const std::string partial = "GET / HTTP/1.1";
            send(sock, partial.data(), partial.size(), 0);
            char byte;
            const auto start = std::chrono::steady_clock::now();
            const auto received = recv(sock, &byte, 1, 0);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            check("upstream read timeout closes an incomplete request", connected && received == 0 && elapsed < std::chrono::seconds(1));
            close(sock);
#endif
            server.stop();
            listener.join();
        }
    }
    std::printf("httpAdmission: %d failures\n", failures);
    return failures ? 1 : 0;
}
