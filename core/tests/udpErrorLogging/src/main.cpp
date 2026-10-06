// Deterministic log accounting (#398): once-only gates stand in for a clock
// held within the interval; reconstructing a gate advances to the next report.
#include "tc/network/tcUdpSocket.h"
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <future>
#include <memory>
#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace trussc::internal {
struct UdpSocketTestAccess {
    using Kind = UdpSocket::ErrorKind;
    static void openGate(UdpSocket& socket, Kind kind) {
        // Only used with no concurrent calls into this socket.
        auto& gate = socket.errorLogs_[static_cast<size_t>(kind)].gate;
        std::destroy_at(&gate);
        std::construct_at(&gate); // once-only: no elapsed-time assumptions
    }
    static void freezeGates(UdpSocket& socket) {
        for (auto kind : {Kind::Resolve, Kind::Send, Kind::Receive}) openGate(socket, kind);
    }
    static void fail(UdpSocket& socket, Kind kind) {
        socket.notifyError(kind, "injected failure", 42);
    }
    static void recover(UdpSocket& socket, Kind kind) { socket.notifyRecovery(kind); }
    static int port(UdpSocket& socket) {
        sockaddr_in address{};
        socklen_t size = sizeof(address);
        if (getsockname(socket.socket_, reinterpret_cast<sockaddr*>(&address), &size) != 0) return 0;
        return ntohs(address.sin_port);
    }
};
} // namespace trussc::internal

namespace {
using namespace trussc;
using Access = internal::UdpSocketTestAccess;
using Kind = Access::Kind;
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

struct Logs {
    std::mutex mutex;
    std::vector<std::string> lines;
    EventListener listener = getLogger().onLog.listen([this](LogEventArgs& args) {
        if (args.message.find("UdpSocket: ") == 0) {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back(args.message);
        }
    });
    std::vector<std::string> take() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> result;
        result.swap(lines);
        return result;
    }
};

void accounting() {
    Logs logs;
    {
        UdpSocket fresh;
        check("send without a socket fails", !fresh.send("x"));
        check("production gate logs its first failure", logs.take() ==
              std::vector<std::string>{"UdpSocket: Socket not created (code: 0)"});
    }
    UdpSocket socket;
    Access::freezeGates(socket);
    int callbacks = 0;
    auto listener = socket.onError.listen([&](UdpErrorEventArgs& args) {
        ++callbacks;
        check("error payload unchanged", args.message == "injected failure" && args.errorCode == 42);
    });
    for (auto kind : {Kind::Resolve, Kind::Send, Kind::Receive}) {
        Access::fail(socket, kind);
        check("each kind logs its first failure immediately", logs.take() ==
              std::vector<std::string>{"UdpSocket: injected failure (code: 42)"});
        Access::fail(socket, kind);
        Access::fail(socket, kind);
        check("closed gate suppresses repeated log lines", logs.take().empty());
        Access::openGate(socket, kind);
        Access::fail(socket, kind);
        check("next report contains suppressed count", logs.take() ==
              std::vector<std::string>{"UdpSocket: injected failure (code: 42) (+2 more since the last report)"});
        Access::recover(socket, kind);
        check("report resets the count", logs.take().empty());
        Access::fail(socket, kind);
        Access::fail(socket, kind);
        Access::fail(socket, kind);
        Access::recover(socket, kind);
        const std::string name = kind == Kind::Resolve ? "resolve" : kind == Kind::Send ? "send" : "receive";
        check("recovery reports the held count", logs.take() ==
              std::vector<std::string>{"UdpSocket: " + name + " recovered after 3 more failures"});
        Access::recover(socket, kind);
        check("recovery resets the count", logs.take().empty());
    }
    check("every failure fires onError", callbacks == 21);
    UdpSocket other;
    Access::freezeGates(other);
    Access::fail(other, Kind::Send);
    check("gates belong to each socket", logs.take().size() == 1);

    // Interleave kinds: a successful send must not consume receive counts.
    Access::fail(socket, Kind::Send);
    Access::fail(socket, Kind::Receive);
    Access::recover(socket, Kind::Send);
    check("send recovery is independent", logs.take() ==
          std::vector<std::string>{"UdpSocket: send recovered after 1 more failures"});
    Access::recover(socket, Kind::Receive);
    check("receive count remains pending", logs.take() ==
          std::vector<std::string>{"UdpSocket: receive recovered after 1 more failures"});
}

void concurrentAccounting() {
    Logs logs;
    UdpSocket socket;
    Access::freezeGates(socket);
    std::atomic<int> callbacks{0};
    auto listener = socket.onError.listen([&](UdpErrorEventArgs&) { ++callbacks; });
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([&] {
        for (int j = 0; j < 1000; ++j) Access::fail(socket, Kind::Send);
    });
    for (auto& thread : threads) thread.join();
    check("concurrent failures log once and notify every time", logs.take().size() == 1 && callbacks == 4000);
    threads.clear();
    for (int i = 0; i < 4; ++i) threads.emplace_back([&] { Access::recover(socket, Kind::Send); });
    for (auto& thread : threads) thread.join();
    check("concurrent recovery accounts for every suppressed failure once", logs.take() ==
          std::vector<std::string>{"UdpSocket: send recovered after 3999 more failures"});

    // Both callback types can re-enter error bookkeeping without deadlocking.
    auto errorListener = socket.onError.listen([&](UdpErrorEventArgs&) { Access::recover(socket, Kind::Send); });
    bool reentered = false;
    auto logListener = getLogger().onLog.listen([&](LogEventArgs& args) {
        if (!reentered && args.message.find("UdpSocket: send recovered") == 0) {
            reentered = true;
            Access::fail(socket, Kind::Send);
        }
    });
    Access::fail(socket, Kind::Send);
    check("log and error listeners may re-enter", reentered && callbacks == 4002 && logs.take().size() == 2);
}

void loopback() {
    Logs logs;
    UdpSocket receiver, sender;
    Access::freezeGates(receiver);
    Access::freezeGates(sender);
    if (!receiver.bind(0, false)) { check("bind loopback fixture", false); return; }
    const int port = Access::port(receiver);
    check("loopback fixture has a port", port > 0);
    check("set receive timeout", receiver.setReceiveTimeout(2000));
    char buffer[8];
    auto inject = [](UdpSocket& socket, Kind kind) {
        Access::fail(socket, kind);
        Access::fail(socket, kind);
    };
    inject(sender, Kind::Resolve);
    inject(sender, Kind::Send);
    logs.take();
    check("sendTo succeeds", sender.sendTo("127.0.0.1", port, "x"));
    check("sendTo flushes resolve and send counts", logs.take() == std::vector<std::string>{
          "UdpSocket: resolve recovered after 1 more failures", "UdpSocket: send recovered after 1 more failures"});
    check("receive loopback data", receiver.receive(buffer, sizeof(buffer)) == 1);

    inject(sender, Kind::Resolve); // gate is still closed: two suppressed failures
    logs.take();
    check("connect succeeds", sender.connect("127.0.0.1", port));
    check("connect flushes resolve counts", logs.take() ==
          std::vector<std::string>{"UdpSocket: resolve recovered after 2 more failures"});
    // A datagram larger than IPv4 UDP permits fails deterministically.
    const std::string oversized(65536, 'x');
    check("send rejects an oversized datagram", !sender.send(oversized));
    check("sendTo rejects an oversized datagram", !sender.sendTo("127.0.0.1", port, oversized));
    check("both send variants use the same gate", logs.take().empty());
    check("connected send succeeds", sender.send("x"));
    check("send flushes its count", logs.take() ==
          std::vector<std::string>{"UdpSocket: send recovered after 2 more failures"});
    check("drain connected datagram", receiver.receive(buffer, sizeof(buffer)) == 1);

    for (int mode = 0; mode < 3; ++mode) {
        for (bool empty : {false, true}) {
            Access::openGate(receiver, Kind::Receive);
            inject(receiver, Kind::Receive);
            logs.take();
            std::promise<void> recovered;
            auto result = recovered.get_future();
            auto logListener = getLogger().onLog.listen([&](LogEventArgs& args) {
                if (args.message == "UdpSocket: receive recovered after 1 more failures") recovered.set_value();
            });
            if (mode != 0) {
                receiver.setUseThread(mode == 2);
                receiver.startReceiving();
            }
            check("queue receive recovery datagram", sender.send(empty ? "" : "x"));
            if (mode == 0) check("synchronous receive succeeds", receiver.receive(buffer, sizeof(buffer)) == (empty ? 0 : 1));
            if (mode == 1) receiver.processNetwork();
            check("receive success logs recovery (sync/poll/thread, data/empty)",
                  result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
            if (mode != 0) receiver.stopReceiving();
            check("receive recovery logs once", logs.take() ==
                  std::vector<std::string>{"UdpSocket: receive recovered after 1 more failures"});
        }
    }
}
} // namespace

TC_CORE_TEST_MAIN() {
    const auto oldLevel = trussc::getLogger().getConsoleLogLevel();
    trussc::getLogger().setConsoleLogLevel(trussc::LogLevel::Silent);
    accounting();
    concurrentAccounting();
    loopback();
    trussc::getLogger().setConsoleLogLevel(oldLevel);
    return failures ? 1 : 0;
}
