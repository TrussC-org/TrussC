#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"
#include "tc/network/tcTcpServer.h"
#include "tc/network/tcUdpSocket.h"
#include "tc/utils/tcLog.h"
#include <cerrno>
#include <barrier>

namespace trussc::internal {
struct TcpServerBindAddressTestAccess {
    static auto descriptor(TcpServer& server) { return server.state_->serverSocket_; }
};
struct UdpSocketBindAddressTestAccess {
    static auto descriptor(UdpSocket& socket) {
        return sharedLoad(socket.state_)->socket_->handle.load();
    }
    static auto run(UdpSocket& socket) { return sharedLoad(socket.state_); }
    static std::string preference(UdpSocket& socket) {
        auto config = run(socket)->bindConfiguration_;
        std::lock_guard<std::mutex> lock(config->mutex);
        return config->error.empty() ? config->address : config->error;
    }
    static bool bound(UdpSocket& socket) { return sharedLoad(socket.state_)->bound_; }
};
}

namespace {
using TcpAccess = internal::TcpServerBindAddressTestAccess;
using UdpAccess = internal::UdpSocketBindAddressTestAccess;

bool boundTo(TestSocket descriptor, uint32_t address) {
    sockaddr_in actual{};
#ifdef _WIN32
    int size = sizeof(actual);
#else
    socklen_t size = sizeof(actual);
#endif
    return getsockname(descriptor, reinterpret_cast<sockaddr*>(&actual), &size) == 0 &&
           actual.sin_family == AF_INET && actual.sin_addr.s_addr == htonl(address) && actual.sin_port != 0;
}

struct Logs {
    vector<string> errors, warnings;
    EventListener listener = getLogger().onLog.listen([this](LogEventArgs& e) {
        if (e.level == LogLevel::Error) errors.push_back(e.message);
        if (e.level == LogLevel::Warning) warnings.push_back(e.message);
    });
    void clear() { errors.clear(); warnings.clear(); }
};

void checkInvalidDiagnostic(const string& address, const string& error) {
    check("Invalid address diagnostic gives numeric loopback guidance",
          error.find("use a numeric IPv4 address such as 127.0.0.1 (names are not resolved)") != string::npos);
    check("Invalid address diagnostic is bounded and has no raw control characters",
          error.size() <= 256 && all_of(error.begin(), error.end(), [](unsigned char c) {
              return c >= 0x20 && c < 0x7f;
          }));
    if (address == "localhost" || address == "machine.example") {
        check("Hostname diagnostic names the rejected value",
              error == "Invalid bind address \"" + address +
                       "\": use a numeric IPv4 address such as 127.0.0.1 (names are not resolved)");
    }
    if (address.size() > 32) {
        check("Overlong address preview is truncated with an ellipsis",
              error.find("Invalid bind address \"" + string(32, '1') + "...\":") == 0);
    }
    if (address.find('\0') != string::npos)
        check("Embedded NUL is escaped in diagnostic", error.find("127.0.0.1\\x00junk") != string::npos);
    if (address == "bad\n\r\t\x1b\x7f\"\\")
        check("Controls, quotes and backslashes are escaped in diagnostic",
              error.find("bad\\x0a\\x0d\\x09\\x1b\\x7f\\\"\\\\") != string::npos);
}

// Model a setter paused immediately after loading the State, without OS sockets.
// The barriers force replacement to finish before that setter writes its preference.
void udpReplacementRace() {
    for (bool closing : {false, true}) {
        for (const string address : {string("127.0.0.1"), string("localhost")}) {
            UdpSocket socket;
            std::barrier gate(2);
            std::thread setter([&] {
                auto loaded = UdpAccess::run(socket);
                gate.arrive_and_wait();
                gate.arrive_and_wait();
                loaded->setBindAddress(address);
            });
            gate.arrive_and_wait();
            if (closing) socket.close();
            else socket.stopReceiving();
            gate.arrive_and_wait();
            setter.join();
            const auto saved = UdpAccess::preference(socket);
            check(closing ? "UDP close retains a setter on the replaced State" :
                            "UDP stopReceiving retains a setter on the replaced State",
                  address == "localhost" ? saved.find("Invalid bind address \"localhost\"") == 0 : saved == address);

            // Exercise the public calls too, with both operations released together.
            constexpr int rounds = 20000;
            int lost = 0;
            std::thread publicSetter([&] {
                for (int round = 0; round < rounds; ++round) {
                    gate.arrive_and_wait();
                    socket.setBindAddress(address);
                    gate.arrive_and_wait();
                }
            });
            for (int round = 0; round < rounds; ++round) {
                socket.setBindAddress("");
                gate.arrive_and_wait();
                if (closing) socket.close();
                else socket.stopReceiving();
                gate.arrive_and_wait();
                const auto preference = UdpAccess::preference(socket);
                if (address == "localhost" ? preference.find("Invalid bind address \"localhost\"") != 0 :
                                             preference != address) ++lost;
            }
            publicSetter.join();
            check(closing ? "UDP public setter/close barrier race loses no preferences" :
                            "UDP public setter/stopReceiving barrier race loses no preferences", lost == 0);
        }
    }
}

void tcpAddresses() {
    TcpServer server;
    Logs logs;
    string error;
    int errorCode = 0;
    auto listener = server.onError.listen([&](TcpServerErrorEventArgs& e) {
        error = e.message; errorCode = e.errorCode;
    });
    check("TCP default binds every interface", server.start(0) && boundTo(TcpAccess::descriptor(server), INADDR_ANY));
    server.stop();
    for (const string& address : {string(""), string("0.0.0.0"), string("127.0.0.1")}) {
        server.setBindAddress(address);
        check("TCP explicit address binds exactly", server.start(0) &&
              boundTo(TcpAccess::descriptor(server), address == "127.0.0.1" ? INADDR_LOOPBACK : INADDR_ANY));
        server.stop();
    }
    const vector<pair<string, string>> rejected{
        {"bad", "Invalid bind address"}, {"localhost", "Invalid bind address"},
        {"machine.example", "Invalid bind address"}, {"bad\n\r\t\x1b\x7f\"\\", "Invalid bind address"},
        {" 127.0.0.1", "Invalid bind address"}, {"127.0.0.1 ", "Invalid bind address"},
        {"256.0.0.1", "Invalid bind address"}, {"127.1", "Invalid bind address"},
        {"127.00.0.1", "Invalid bind address"}, {"2130706433", "Invalid bind address"},
        {string("127.0.0.1\0junk", 14), "Invalid bind address"},
        {string(1024, '1'), "Invalid bind address"},
        {"::1", "IPv6 is not supported"}, {"2001:db8:1234:5678::1", "IPv6 is not supported"},
        {"192.0.2.1", "Bind address is not available on this machine"}
    };
    for (const auto& [address, diagnostic] : rejected) {
        error.clear(); logs.clear();
        server.setBindAddress(address);
        // The preceding explicit bind or retry used loopback, then stopped.
        check("TCP rejects invalid address after valid start/stop without reusing it",
              !server.start(0) && !server.isRunning() && TcpAccess::descriptor(server) == INVALID_SOCKET);
        check("TCP emits named error and log", error.find(diagnostic) == 0 &&
              logs.errors.size() == 1 && logs.errors[0].find(error) != string::npos);
        if (diagnostic == "Invalid bind address") checkInvalidDiagnostic(address, error);
        if (diagnostic == "IPv6 is not supported")
            check("IPv6 diagnostic names the rejected value", error.find("\"" + address + "\"") != string::npos);
        if (address == "192.0.2.1") {
#ifdef _WIN32
            check("TCP preserves address-unavailable code", errorCode == WSAEADDRNOTAVAIL);
#else
            check("TCP preserves address-unavailable code", errorCode == EADDRNOTAVAIL);
#endif
        }
        server.setBindAddress("127.0.0.1");
        check("TCP restarts after rejected address", server.start(0) && boundTo(TcpAccess::descriptor(server), INADDR_LOOPBACK));
        server.stop();
    }
    server.setBindAddress("127.0.0.1");
    check("TCP running setter fixture", server.start(0));
    logs.clear();
    server.setBindAddress("");
    check("TCP running change warns and keeps loopback", logs.warnings.size() == 1 &&
          logs.warnings[0].find("ignored while running") != string::npos &&
          boundTo(TcpAccess::descriptor(server), INADDR_LOOPBACK));
    check("TCP pending address applies on next start", server.start(0) && boundTo(TcpAccess::descriptor(server), INADDR_ANY));
    server.setBindAddress("localhost");
    check("TCP pending invalid address does not change active socket", boundTo(TcpAccess::descriptor(server), INADDR_ANY));
    error.clear();
    check("TCP pending invalid address fails restart", !server.start(0) && !server.isRunning() && error.find("Invalid bind address") == 0);
}

void udpAddresses() {
    UdpSocket socket;
    Logs logs;
    string error;
    int errorCode = 0;
    auto listener = socket.onError.listen([&](UdpErrorEventArgs& e) {
        error = e.message; errorCode = e.errorCode;
    });
    check("UDP default binds every interface", socket.bind(0, false) && boundTo(UdpAccess::descriptor(socket), INADDR_ANY));
    socket.close();
    for (const string& address : {string(""), string("0.0.0.0"), string("127.0.0.1")}) {
        socket.setBindAddress(address);
        check("UDP explicit address binds exactly", socket.bind(0, false) &&
              boundTo(UdpAccess::descriptor(socket), address == "127.0.0.1" ? INADDR_LOOPBACK : INADDR_ANY));
        socket.close();
    }
    const vector<pair<string, string>> rejected{
        {"bad", "Invalid bind address"}, {"localhost", "Invalid bind address"},
        {"machine.example", "Invalid bind address"}, {"bad\n\r\t\x1b\x7f\"\\", "Invalid bind address"},
        {" 127.0.0.1", "Invalid bind address"}, {"127.0.0.1 ", "Invalid bind address"},
        {"256.0.0.1", "Invalid bind address"}, {"127.1", "Invalid bind address"},
        {"127.00.0.1", "Invalid bind address"}, {"2130706433", "Invalid bind address"},
        {string("127.0.0.1\0junk", 14), "Invalid bind address"},
        {string(1024, '1'), "Invalid bind address"},
        {"::1", "IPv6 is not supported"}, {"2001:db8:1234:5678::1", "IPv6 is not supported"},
        {"192.0.2.1", "Bind address is not available on this machine"}
    };
    for (const auto& [address, diagnostic] : rejected) {
        error.clear(); logs.clear();
        socket.setBindAddress(address);
        // The preceding explicit bind or retry used loopback, then closed.
        check("UDP rejects invalid address after valid bind/close without reusing it",
              !socket.bind(0, false) && !UdpAccess::bound(socket) && !socket.isReceiving() &&
              socket.getLocalPort() == 0 && !boundTo(UdpAccess::descriptor(socket), INADDR_LOOPBACK) &&
              !boundTo(UdpAccess::descriptor(socket), INADDR_ANY));
        check("UDP emits named error and log", error.find(diagnostic) == 0 &&
              logs.errors.size() == 1 && logs.errors[0].find(error) != string::npos);
        if (diagnostic == "Invalid bind address") checkInvalidDiagnostic(address, error);
        if (diagnostic == "IPv6 is not supported")
            check("IPv6 diagnostic names the rejected value", error.find("\"" + address + "\"") != string::npos);
        if (address == "192.0.2.1") {
#ifdef _WIN32
            check("UDP preserves address-unavailable code", errorCode == WSAEADDRNOTAVAIL);
#else
            check("UDP preserves address-unavailable code", errorCode == EADDRNOTAVAIL);
#endif
        }
        socket.setBindAddress("127.0.0.1");
        check("UDP retries after rejected address without close", socket.bind(0, false) && boundTo(UdpAccess::descriptor(socket), INADDR_LOOPBACK));
        socket.close();
    }
    for (bool receiving : {false, true}) {
        socket.setBindAddress("127.0.0.1");
        check("UDP bound setter fixture", socket.bind(0, receiving));
        logs.clear();
        socket.setBindAddress("");
        check("UDP bound change warns and keeps loopback", logs.warnings.size() == 1 &&
              logs.warnings[0].find("ignored while bound") != string::npos &&
              boundTo(UdpAccess::descriptor(socket), INADDR_LOOPBACK));
        socket.stopReceiving();
        check("UDP stop receiving preserves active bind", boundTo(UdpAccess::descriptor(socket), INADDR_LOOPBACK));
        socket.close();
        check("UDP pending address survives stop and close", socket.bind(0, false) && boundTo(UdpAccess::descriptor(socket), INADDR_ANY));
        socket.setBindAddress("localhost");
        check("UDP pending invalid address preserves socket", boundTo(UdpAccess::descriptor(socket), INADDR_ANY));
        socket.close(); error.clear();
        check("UDP pending invalid address fails rebind", !socket.bind(0, false) && error.find("Invalid bind address") == 0);
    }
    socket.setBindAddress("127.0.0.1");
    UdpSocket moved(std::move(socket));
    check("UDP stopped move preserves chosen bind address", moved.bind(0, false) && boundTo(UdpAccess::descriptor(moved), INADDR_LOOPBACK));
}
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    Watchdog watchdog;
    udpReplacementRace();
    if (argc > 1 && string(argv[1]) == "--race-only") return failures ? 1 : 0;
    tcpAddresses();
    udpAddresses();
    return failures ? 1 : 0;
}
