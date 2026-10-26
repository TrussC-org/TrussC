#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"
#include "tc/network/tcTcpServer.h"
#include <array>
#include <future>
#include <limits>

namespace trussc::internal {
struct TcpServerTestAccess {
    static auto state(TcpServer& server) { return server.state_; }
    static size_t bytes(TcpServer& server) {
        auto s = state(server);
        std::lock_guard<std::mutex> lock(s->sendMutex_);
        return s->totalSendBytes_;
    }
    static std::array<uint64_t, 4> rejected(TcpServer& server) {
        auto s = state(server);
        std::lock_guard<std::mutex> lock(s->sendMutex_);
        return {s->rejectedSends_[0], s->rejectedSends_[1], s->rejectedSends_[2], s->rejectedSends_[3]};
    }
    static size_t waiting(TcpServer& server) {
        auto s = state(server);
        std::lock_guard<std::mutex> lock(s->sendMutex_);
        return s->waitingSenders_;
    }
    static size_t workers(TcpServer& server) { return state(server)->workerClients_; }
    static uint64_t connections(TcpServer& server) { return state(server)->rejectedConnections_; }
    static vector<int> broadcastOrder(TcpServer& server) {
        vector<int> ids;
        for (auto& [id, ch] : state(server)->snapshotChannels()) ids.push_back(id);
        return ids;
    }
    static void failAfterRejection(TcpServer& server, bool multiple) {
        using Reports = TcpServer::State::SendRejections;
        state(server)->beforeSendEnqueueForTest_ = multiple
            ? +[](const Reports& reports) {
                if (reports[1].warn && reports[3].warn) throw std::bad_alloc();
            }
            : +[](const Reports& reports) {
                if (reports[3].warn) throw std::bad_alloc();
            };
    }
    static void clearAllocationFailure(TcpServer& server) {
        state(server)->beforeSendEnqueueForTest_ = nullptr;
    }
    static size_t entries(TcpServer& server, int id) {
        auto ch = state(server)->findChannel(id);
        std::lock_guard<std::mutex> lock(ch->mutex);
        return ch->pendingEntries;
    }
    static bool failWrites(TcpServer& server, int id) {
        auto ch = state(server)->findChannel(id);
        std::lock_guard<std::mutex> lock(ch->mutex);
#ifdef _WIN32
        return ::shutdown(ch->socket, SD_SEND) == 0;
#else
        return ::shutdown(ch->socket, SHUT_WR) == 0;
#endif
    }
    static void closeChannel(TcpServer& server, int id) {
        auto s = state(server);
        s->shutChannel(s->findChannel(id));
    }
    static void seedRejections(TcpServer& server, uint64_t value) {
        auto s = state(server);
        std::lock_guard<std::mutex> lock(s->sendMutex_);
        s->rejectedConnections_ = value;
        for (auto& total : s->rejectedSends_) total = value;
    }
    static SendResult rejectAtBound(TcpServer& server, size_t reason) {
        auto s = state(server);
        auto ch = std::make_shared<TcpSendChannel>();
        if (reason == 1) ch->pendingBytes = s->sendAsyncBufferSize_;
        if (reason == 2) ch->pendingEntries = 4096;
        TcpSendItem item;
        item.size = reason == 0 || reason == 3 ? 2 : 1;
        s->setSendItemLimit(reason == 0 ? 1 : 2);
        s->setTotalSendBufferSize(1);
        const size_t size = item.size;
        TcpServer::State::SendRejections reports{};
        auto result = s->enqueue(ch, 1, std::move(item), reports, size,
            [](TcpSendItem&, const std::shared_ptr<void>&) { std::_Exit(2); });
        s->reportSendRejections(reports);
        return result;
    }
    static size_t handles(TcpServer& server) {
        auto s = state(server);
        std::shared_lock<std::shared_mutex> lock(s->clientsMutex_);
        return s->clientThreads_.size() + s->clientWriters_.size() +
            s->finishedClientThreads_.size() + s->finishedWriters_.size();
    }
    static std::shared_ptr<const std::vector<char>> payload(TcpServer& server, int id) {
        auto ch = state(server)->findChannel(id);
        std::lock_guard<std::mutex> lock(ch->mutex);
        return ch->queue.front().owned;
    }
    static bool allocationFailure(TcpServer& server, int id) {
        auto s = state(server);
        try {
            TcpSendItem item;
            item.size = 1;
            TcpServer::State::SendRejections reports{};
            s->enqueue(s->findChannel(id), id, std::move(item), reports, 1,
                [](TcpSendItem&, const std::shared_ptr<void>&) { throw std::bad_alloc(); });
        } catch (const std::bad_alloc&) { return true; }
        return false;
    }
};
} // namespace trussc::internal

namespace {
using Access = internal::TcpServerTestAccess;
TestSocket peer(int port, bool narrowWindow = false) {
    TestSocket fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (narrowWindow) {
        const int bytes = 4096;
        if (::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bytes), sizeof(bytes)) != 0) _Exit(1);
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        check("loopback connect", false);
        _Exit(1);
    }
    return fd;
}
void require(const char* label, bool ok) {
    check(label, ok);
    if (!ok) { fflush(nullptr); _Exit(1); }
}
struct Warnings {
    atomic<int> sends{0}, connections{0};
    EventListener listener = getLogger().onLog.listen([this](LogEventArgs& e) {
        if (e.level != LogLevel::Warning) return;
        if (e.message.find("whole send rejected") != string::npos) ++sends;
        if (e.message.find("new connection closed") != string::npos) ++connections;
    });
};
void connectionBounds() {
    TcpServer server;
    Warnings warnings;
    check("zero and negative client configurations fail", !server.start(0, 0) && !server.start(0, -1));
    require("default server starts", server.start(0));
    const int port = server.getPort();
    check("invalid client limit preserves the running server", !server.start(0, 0) && server.isRunning() && server.getPort() == port);
    vector<TestSocket> peers;
    for (int i = 0; i < 64; ++i) peers.push_back(peer(server.getPort()));
    require("default admits 64 clients", until([&] { return server.getClientCount() == 64; }));
    for (int i = 0; i < 32; ++i) {
        auto fd = peer(server.getPort());
        CLOSE_SOCKET(fd);
    }
    require("excess connection totals are exact", until([&] { return Access::connections(server) == 32; }));
    check("excess connections create no worker slots", Access::workers(server) == 64);
    check("connection flood warns once", warnings.connections == 1);
    server.stop();
    for (auto fd : peers) CLOSE_SOCKET(fd);
    check("stop releases workers and handles", Access::workers(server) == 0 && Access::handles(server) == 0);
    require("bounded churn server starts", server.start(0, 1));
    for (int i = 0; i < 20; ++i) {
        auto fd = peer(server.getPort());
        require("churn client registers", until([&] { return server.getClientCount() == 1; }));
        CLOSE_SOCKET(fd);
        require("churn reaps closed workers without another connection", until([&] {
            return Access::workers(server) == 0 && Access::handles(server) == 0;
        }));
    }
    // A callback that disconnects its client still owns a worker slot.
    promise<void> entered, release;
    auto proceed = release.get_future().share();
    auto ready = entered.get_future();
    auto listener = server.onReceive.listen([&](TcpServerReceiveEventArgs& e) {
        server.disconnectClient(e.clientId);
        entered.set_value();
        proceed.wait();
    });
    auto fd = peer(server.getPort());
    require("callback client registers", until([&] { return server.getClientCount() == 1; }));
    ::send(fd, "x", 1, 0);
    require("closed client callback is held", ready.wait_for(chrono::seconds(10)) == future_status::ready);
    for (int i = 0; i < 10; ++i) {
        auto excess = peer(server.getPort());
        CLOSE_SOCKET(excess);
    }
    require("closed callback still consumes admission", until([&] { return Access::connections(server) == 42; }));
    check("closed callback does not accumulate more workers", Access::workers(server) == 1 && server.getClientCount() == 0);
    check("connection Warning and totals survive restart", warnings.connections == 1 && Access::connections(server) == 42);
    release.set_value();
    CLOSE_SOCKET(fd);
    require("released callback is reaped", until([&] { return Access::workers(server) == 0 && Access::handles(server) == 0; }));
    server.stop();
}

void sendCredits() {
    TcpServer server;
    Warnings warnings;
    require("send-credit server starts", server.start(0, 4));
    vector<TestSocket> peers;
    for (int i = 0; i < 4; ++i) peers.push_back(peer(server.getPort()));
    require("four peers register", until([&] { return server.getClientCount() == 4; }));
    auto ids = server.getClientIds();
    // Hold each writer after a real socket completion. No assumption about
    // kernel buffer size, scheduling speed or how long a socket stays blocked.
    promise<void> release;
    auto proceed = release.get_future().share();
    atomic<int> entered{0};
    auto listener = server.onSendComplete.listen([&](TcpSendCompleteEventArgs&) {
        if (entered.fetch_add(1) < 4) proceed.wait();
    });
    for (int id : ids) require("writer gate send accepted", bool(server.sendAsync(id, nullptr, 0)));
    require("all writers held", until([&] { return entered == 4; }));
    check("failed allocation propagates", Access::allocationFailure(server, ids[0]));
    check("failed preparation releases credits", Access::bytes(server) == 0);
    server.setSendAsyncBufferSize(8);
    server.setSendItemLimit(16);
    server.setTotalSendBufferSize(64);
    server.setSendAsyncBufferSize(0);
    server.setSendItemLimit(0);
    server.setTotalSendBufferSize(0);
    check("zero high-water preserves configuration", server.getSendAsyncBufferSize() == 8);
    char data[17]{};
    for (int i = 0; i < 3; ++i)
        check("oversize owned send rejected whole", server.sendAsync(ids[0], data, 17).error == SendError::QueueFull);
    vector<char> reserved;
    reserved.reserve(17);
    check("rvalue capacity counts before move", server.sendAsync(ids[0], std::move(reserved)).error == SendError::QueueFull && reserved.capacity() >= 17);
    check("one item crosses the high-water mark", bool(server.sendAsync(ids[0], data, 16)));
    // A rejection must precede dereferencing/copying the supplied payload.
    for (int i = 0; i < 3; ++i)
        check("full queue refuses before copy", server.sendAsync(ids[0], nullptr, 1).error == SendError::QueueFull);
    bool emptyEntriesAdmitted = true;
    for (int i = 0; i < 4096; ++i)
        emptyEntriesAdmitted &= bool(server.sendAsync(ids[1], nullptr, 0));
    require("4096 empty entries admitted", emptyEntriesAdmitted);
    for (int i = 0; i < 3; ++i)
        check("empty sends cannot evade entry credits", server.sendAsync(ids[1], nullptr, 0).error == SendError::QueueFull);
    server.setSendAsyncBufferSize(128);
    server.setTotalSendBufferSize(32);
    check("second item fits aggregate exactly", bool(server.sendAsync(ids[2], data, 16)));
    for (int i = 0; i < 3; ++i)
        check("aggregate refuses before copying", server.sendAsync(ids[3], nullptr, 1).error == SendError::QueueFull);
    check("retained bytes bounded across clients", Access::bytes(server) == 32);
    auto rejected = Access::rejected(server);
    check("all rejection totals exact", rejected == array<uint64_t, 4>{4, 3, 3, 3});
    check("each send bound warns once", warnings.sends == 4);
    // Concurrent producers all use the same aggregate admission lock.
    server.setTotalSendBufferSize(64);
    atomic<int> admitted{0};
    vector<thread> producers;
    for (int i = 0; i < 16; ++i) producers.emplace_back([&] {
        if (server.sendAsync(ids[3], data, 16)) ++admitted;
    });
    for (auto& producer : producers) producer.join();
    check("concurrent producers share exact credits", admitted == 2 && Access::bytes(server) == 64);
    check("concurrent rejection totals exact", Access::rejected(server)[3] == 17);
    check("broadcast recipients cannot evade aggregate admission", server.broadcastAsync(nullptr, 1) == 0);
    check("broadcast counts each rejected recipient", Access::rejected(server) == array<uint64_t, 4>{4, 3, 4, 20});
    check("queries do not reset totals", Access::rejected(server) == array<uint64_t, 4>{4, 3, 4, 20} && warnings.sends == 4);
    release.set_value();
    server.stop();
    for (auto fd : peers) CLOSE_SOCKET(fd);
    check("cancel and disconnect release all aggregate credits", Access::bytes(server) == 0);
    check("cancel and disconnect reap all workers", Access::workers(server) == 0 && Access::handles(server) == 0);
}
void broadcastAndSync() {
    TcpServer server;
    require("broadcast fixture starts", server.start(0, 2));
    auto first = peer(server.getPort()), second = peer(server.getPort());
    require("broadcast peers registered", until([&] { return server.getClientCount() == 2; }));
    auto ids = server.getClientIds();
    promise<void> release;
    auto proceed = release.get_future().share();
    atomic<int> entered{0};
    atomic<int> completed{0};
    auto listener = server.onSendComplete.listen([&](TcpSendCompleteEventArgs&) {
        ++completed;
        if (entered.fetch_add(1) < 2) proceed.wait();
    });
    for (int id : ids) require("broadcast gate accepted", bool(server.sendAsync(id, nullptr, 0)));
    require("broadcast writers held", until([&] { return entered == 2; }));
    server.setSendItemLimit(16);
    server.setTotalSendBufferSize(32);
    char bytes[16]{};
    check("broadcast admits both recipients at exact aggregate", server.broadcastAsync(bytes, 16) == 2);
    auto retained = Access::payload(server, ids[0]);
    check("broadcast shares a complete payload", retained == Access::payload(server, ids[1]) && retained->size() == 16);
    check("fanout charges each recipient", Access::bytes(server) == 32);
    check("repeat broadcast refuses whole units", server.broadcastAsync(nullptr, 1) == 0);
    release.set_value();
    require("broadcast completes exactly once per recipient", until([&] { return completed == 4; }));
    check("shared alias retains its original allocation credit", Access::bytes(server) == 16);
    retained.reset();
    check("last shared alias releases original credit", Access::bytes(server) == 0);
    server.stop();
    CLOSE_SOCKET(first); CLOSE_SOCKET(second);

    require("sync fixture restarts", server.start(0, 2));
    check("send rejection totals survive restart", Access::rejected(server)[3] == 2);
    first = peer(server.getPort()); second = peer(server.getPort());
    require("sync peers register", until([&] { return server.getClientCount() == 2; }));
    ids = server.getClientIds();
    promise<void> releaseSync;
    auto syncProceed = releaseSync.get_future().share();
    entered = 0;
    listener = server.onSendComplete.listen([&](TcpSendCompleteEventArgs&) {
        if (entered.fetch_add(1) < 2) syncProceed.wait();
    });
    for (int id : ids) require("sync gate accepted", bool(server.sendAsync(id, nullptr, 0)));
    require("sync writers held", until([&] { return entered == 2; }));
    server.setTotalSendBufferSize(1);
    require("aggregate occupied by first peer", bool(server.sendAsync(ids[0], bytes, 1)));
    auto syncSend = async(launch::async, [&] { return server.send(ids[1], bytes, 1); });
    require("sync sender waits on aggregate admission", until([&] { return Access::waiting(server) == 1; }));
    server.setTotalSendBufferSize(2);
    require("raising aggregate wakes and admits sync sender", until([&] {
        return Access::waiting(server) == 0 && Access::bytes(server) == 2;
    }));
    server.setTotalSendBufferSize(1);
    auto cancelledSend = async(launch::async, [&] { return server.send(ids[1], bytes, 1); });
    require("lowering aggregate preserves sends and blocks new admission", until([&] { return Access::waiting(server) == 1; }));
    // A real receive callback closes the blocked sender's channel without
    // joining its paused writer. The waiting send must settle on disconnect.
    auto receive = server.onReceive.listen([&](TcpServerReceiveEventArgs& e) { server.disconnectClient(e.clientId); });
    ::send(first, "x", 1, 0);
    ::send(second, "x", 1, 0);
    require("waiting sync send settles on disconnect", cancelledSend.wait_for(chrono::seconds(10)) == future_status::ready);
    check("disconnected waiting sync send reports failure", !cancelledSend.get());
    releaseSync.set_value();
    require("admitted sync send settles on disconnect", syncSend.wait_for(chrono::seconds(10)) == future_status::ready);
    check("disconnected queued sync send reports failure", !syncSend.get());
    server.stop();
    CLOSE_SOCKET(first); CLOSE_SOCKET(second);
    check("sync cancellation returns all credits", Access::bytes(server) == 0);
}

void slowSockets() {
    TcpServer server;
    server.setSendTimeout(0.01f);
    require("slow socket fixture starts", server.start(0, 4));
    vector<TestSocket> peers;
    for (int i = 0; i < 4; ++i) peers.push_back(peer(server.getPort(), true));
    require("slow peers register", until([&] { return server.getClientCount() == 4; }));
    vector<char> bytes(16 * 1024 * 1024, 'x');
    atomic<int> completed{0}, failed{0};
    auto listener = server.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (e.error != SendError::None) ++failed;
        ++completed;
    });
    int accepted = 0;
    for (int id : server.getClientIds()) {
        if (server.sendAsync(id, bytes.data(), bytes.size())) ++accepted;
        check("slow peers stay within aggregate budget", Access::bytes(server) <= 64u * 1024 * 1024);
    }
    require("all admitted slow sends complete or fail", until([&] { return completed == accepted; }));
    check("nonreading sockets exercise write failure", accepted > 0 && failed > 0);
    check("write failure releases payload credits", Access::bytes(server) == 0);
    server.stop();
    for (auto fd : peers) CLOSE_SOCKET(fd);
}
void borrowedStream() {
    TcpServer server;
    require("borrowed stream fixture starts", server.start(0, 1));
    auto fd = peer(server.getPort());
    require("borrowed stream peer registers", until([&] { return server.getClientCount() == 1; }));
    const int id = server.getClientIds()[0];
    promise<void> release;
    auto proceed = release.get_future().share();
    atomic<int> entered{0};
    auto listener = server.onSendComplete.listen([&](TcpSendCompleteEventArgs&) {
        if (entered.fetch_add(1) == 0) proceed.wait();
    });
    require("borrowed stream gate accepted", bool(server.sendAsync(id, nullptr, 0)));
    require("borrowed stream writer held", until([&] { return entered == 1; }));
    server.setSendItemLimit(1024);
    server.setTotalSendBufferSize(1024);
    vector<char> bytes(3 * 1024 * 1024, 's');
    auto sender = async(launch::async, [&] { return server.send(id, bytes.data(), bytes.size()); });
    require("large borrowed send reserves one finite window", until([&] { return Access::bytes(server) == 1024; }));
    check("borrowed payload remains whole beyond owned-item limit", server.getSendAsyncPendingBytes(id) == bytes.size());
    auto reader = async(launch::async, [&] {
        size_t received = 0;
        char buffer[4096];
        bool intact = true;
        while (received < bytes.size()) {
            const int count = static_cast<int>(::recv(fd, buffer, sizeof(buffer), 0));
            if (count <= 0) return false;
            for (int i = 0; i < count; ++i) if (buffer[i] != 's') intact = false;
            received += static_cast<size_t>(count);
        }
        return intact && received == bytes.size();
    });
    release.set_value();
    require("borrowed stream sender completes", sender.wait_for(chrono::seconds(10)) == future_status::ready);
    check("borrowed stream send succeeds", sender.get());
    require("borrowed stream reader completes", reader.wait_for(chrono::seconds(10)) == future_status::ready);
    check("borrowed stream arrives complete", reader.get());
    check("borrowed stream releases window credits", Access::bytes(server) == 0);
    server.stop();
    CLOSE_SOCKET(fd);
}

void rejectionListenerCanStop() {
    TcpServer server;
    require("reentrant Warning fixture starts", server.start(0, 1));
    auto fd = peer(server.getPort());
    require("reentrant Warning peer registers", until([&] { return server.getClientCount() == 1; }));
    server.setSendItemLimit(1);
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Warning && e.message.find("whole send rejected") != string::npos)
            server.stop();
    });
    check("Warning listener may stop without admission locks held", server.sendAsync(server.getClientIds()[0], nullptr, 2).error == SendError::QueueFull);
    check("reentrant stop releases workers and credits", !server.isRunning() && Access::workers(server) == 0 && Access::bytes(server) == 0);
    CLOSE_SOCKET(fd);
}

void exceptionalAsyncReports(bool throwingListener) {
    TcpServer server;
    server.setSendAsyncBufferSize(1);
    server.setTotalSendBufferSize(2);
    require("exceptional fanout starts", server.start(0, 4));
    vector<TestSocket> peers;
    for (int i = 0; i < 4; ++i) peers.push_back(peer(server.getPort()));
    require("exceptional fanout peers register", until([&] { return server.getClientCount() == 4; }));
    const auto ids = Access::broadcastOrder(server);
    promise<void> release;
    auto proceed = release.get_future().share();
    atomic<int> gates{0}, completed{0}, warnings{0};
    auto completion = server.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (e.bytesSent == 0) { ++gates; proceed.wait(); }
        else ++completed;
    });
    for (int id : ids) require("exceptional fanout gate queued", bool(server.sendAsync(id, nullptr, 0)));
    require("exceptional fanout writers held", until([&] { return gates == 4; }));
    require("second recipient high-water occupied", bool(server.sendAsync(ids[1], "q", 1)));
    auto warning = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Warning || e.message.find("whole send rejected") == string::npos) return;
        if (++warnings == 1) {
            release.set_value();
            require("exceptional fanout admitted items finish", until([&] { return completed == 2; }));
        }
        check("exceptional report releases producer payload and credits", Access::bytes(server) == 0);
        if (throwingListener) throw 17;
    });
    // Admit the first recipient, reject the second at high-water and the
    // third at aggregate capacity, then fail preparation of the fourth.
    Access::failAfterRejection(server, true);
    bool caught = false;
    try { server.broadcastAsync("x", 1); }
    catch (const std::bad_alloc&) { caught = true; }
    Access::clearAllocationFailure(server);
    check("fanout preserves original allocation exception", caught);
    check("unwinding delivers both first Warnings exactly once", warnings == 2 &&
        Access::rejected(server) == array<uint64_t, 4>{0, 1, 0, 1});
    // Keep the unfixed implementation's regression failure deterministic too:
    // its missing Warning must not leave the fixture's writers gated.
    if (warnings == 0) release.set_value();
    for (int i = 0; i < 3; ++i) {
        Access::rejectAtBound(server, 1);
        Access::rejectAtBound(server, 3);
    }
    check("exceptional fanout later rejections count without repeated Warnings", warnings == 2 &&
        Access::rejected(server) == array<uint64_t, 4>{0, 4, 0, 4});
    server.stop();
    for (auto fd : peers) CLOSE_SOCKET(fd);
    check("exceptional fanout returns all credits", Access::bytes(server) == 0);
}

void exceptionalSyncReports(bool throwingListener) {
    TcpServer server;
    server.setTotalSendBufferSize(1);
    require("exceptional sync broadcast starts", server.start(0, 3));
    vector<TestSocket> peers;
    for (int i = 0; i < 3; ++i) peers.push_back(peer(server.getPort()));
    require("exceptional sync peers register", until([&] { return server.getClientCount() == 3; }));
    const int source = Access::broadcastOrder(server)[0];
    promise<void> entered, release, returned;
    auto ready = entered.get_future(), done = returned.get_future();
    auto proceed = release.get_future().share();
    atomic<bool> handled{false}, caught{false};
    atomic<int> warnings{0};
    auto warning = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Warning || e.message.find("whole send rejected") == string::npos) return;
        ++warnings;
        check("exceptional sync report holds no admission lock", Access::rejected(server)[3] == 1);
        if (throwingListener) throw 17;
    });
    auto completion = server.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (e.clientId != source || handled.exchange(true)) return;
        entered.set_value();
        proceed.wait();
        try { server.broadcast("x", 1); }
        catch (const std::bad_alloc&) { caught = true; }
        returned.set_value();
    });
    require("exceptional sync writer gate queued", bool(server.sendAsync(source, nullptr, 0)));
    require("exceptional sync writer held", ready.wait_for(chrono::seconds(10)) == future_status::ready);
    require("exceptional sync aggregate occupied", bool(server.sendAsync(source, "q", 1)));
    // Only a writer rejects aggregate admission synchronously. Its own
    // recipient is skipped; rejection at the next one precedes the failure.
    Access::failAfterRejection(server, false);
    release.set_value();
    require("exceptional sync broadcast returns", done.wait_for(chrono::seconds(10)) == future_status::ready);
    Access::clearAllocationFailure(server);
    check("sync broadcast preserves allocation exception and first Warning", caught && warnings == 1);
    for (int i = 0; i < 3; ++i) Access::rejectAtBound(server, 3);
    check("exceptional sync later rejections count without repeated Warnings", warnings == 1 && Access::rejected(server)[3] == 4);
    server.stop();
    for (auto fd : peers) CLOSE_SOCKET(fd);
    check("exceptional sync broadcast returns all credits", Access::bytes(server) == 0);
}

void fanoutWarningCanSend(bool fromReceive) {
    TcpServer server;
    server.setTotalSendBufferSize(1);
    require("fanout reentrant fixture starts", server.start(0, 2));
    auto first = peer(server.getPort()), second = peer(server.getPort());
    require("fanout reentrant peers register", until([&] { return server.getClientCount() == 2; }));
    auto ids = server.getClientIds();
    promise<void> release;
    auto proceed = release.get_future().share();
    atomic<int> gates{0}, completed{0}, warnings{0};
    auto completion = server.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (e.bytesSent == 0) {
            ++gates;
            proceed.wait();
        } else ++completed;
    });
    for (int id : ids) require("fanout reentrant writer gate queued", bool(server.sendAsync(id, nullptr, 0)));
    require("fanout reentrant writers held", until([&] { return gates == 2; }));
    atomic<bool> nested{false};
    auto warning = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Warning || e.message.find("whole send rejected") == string::npos) return;
        ++warnings;
        check("fanout rejection counted before Warning", Access::rejected(server)[3] == 1);
        release.set_value();
        require("admitted fanout completes before nested send", until([&] { return completed == 1; }));
        check("fanout Warning retains no producer allocation credit", Access::bytes(server) == 0);
        // On the old code both writers are idle here, yet this waits forever:
        // the broadcast producer's local payload alias still owns the byte.
        nested = server.send(ids[0], "n", 1);
    });
    promise<int> returned;
    auto done = returned.get_future();
    auto broadcast = [&] { returned.set_value(server.broadcastAsync("x", 1)); };
    EventListener receive;
    thread producer;
    if (fromReceive) {
        receive = server.onReceive.listen([&](TcpServerReceiveEventArgs&) { broadcast(); });
        require("receive-triggered fanout submitted", ::send(first, "r", 1, 0) == 1);
    } else producer = thread(broadcast);
    require(fromReceive ? "receive fanout Warning synchronous send returns" : "producer fanout Warning synchronous send returns",
            done.wait_for(chrono::seconds(10)) == future_status::ready);
    check("fanout admits one recipient and nested send succeeds", done.get() == 1 && nested);
    if (producer.joinable()) producer.join();
    require("fanout and nested send complete once", until([&] { return completed == 2; }));
    check("fanout has one Warning and exact rejection total", warnings == 1 && Access::rejected(server)[3] == 1);
    check("fanout reentrant sends return all credits and entries", Access::bytes(server) == 0 &&
          Access::entries(server, ids[0]) == 0 && Access::entries(server, ids[1]) == 0);
    server.stop();
    CLOSE_SOCKET(first); CLOSE_SOCKET(second);
}

// Gates establish the queue contents before the callback attempts admission.
// The deadline only detects a hang; no assertion depends on socket timing.
enum class CallbackPressure { Aggregate, Entries, HighWater, None };
void callbackAdmission(CallbackPressure pressure, bool syncCompletion = false, bool broadcastFirst = false) {
    const bool entryPressure = pressure == CallbackPressure::Entries;
    const bool highWater = pressure == CallbackPressure::HighWater;
    const bool freeCredits = pressure == CallbackPressure::None;
    const bool destinationWait = entryPressure || highWater;
    TcpServer server;
    Warnings warnings;
    server.setTotalSendBufferSize(1);
    if (highWater) server.setSendAsyncBufferSize(1);
    require("callback admission server starts", server.start(0, 2));
    auto first = peer(server.getPort()), second = peer(server.getPort());
    require("callback peers register", until([&] { return server.getClientCount() == 2; }));
    const auto ids = server.getClientIds();
    const int a = ids[0], b = ids[1];
    promise<void> entered, release, returned, releaseB;
    auto ready = entered.get_future(), done = returned.get_future();
    auto proceed = release.get_future().share(), proceedB = releaseB.get_future().share();
    atomic<bool> firstA{true}, firstB{true};
    atomic<bool> sent{false};
    atomic<int> queueErrors{0};
    auto errors = server.onError.listen([&](TcpServerErrorEventArgs& e) {
        if (e.message == "Send queue full") ++queueErrors;
    });
    auto listener = server.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (e.clientId == b && (entryPressure || highWater) && firstB.exchange(false)) { proceedB.wait(); return; }
        if (e.clientId != a || !firstA.exchange(false)) return;
        entered.set_value();
        proceed.wait();
        if (broadcastFirst) server.broadcast("y", 1);
        sent = server.send(b, "x", 1);
        if (!broadcastFirst) server.broadcast("y", 1); // skips its own writer
        returned.set_value();
    });
    if (entryPressure || highWater) {
        require("entry callback gate queued", bool(server.sendAsync(b, nullptr, 0)));
        require("destination writer paused", until([&] { return !firstB.load(); }));
    }
    future<bool> sender;
    if (syncCompletion) sender = async(launch::async, [&] { return server.send(a, nullptr, 0); });
    else require("empty completion queued", bool(server.sendAsync(a, nullptr, 0)));
    require("source callback paused", ready.wait_for(chrono::seconds(10)) == future_status::ready);
    if (entryPressure) {
        bool accepted = true;
        for (int i = 0; i < 4096; ++i) accepted &= bool(server.sendAsync(b, nullptr, 0));
        require("destination entry limit filled", accepted);
    } else if (highWater) {
        require("destination high-water mark filled", bool(server.sendAsync(b, "q", 1)));
    } else if (!freeCredits) {
        require("producer reserves credit in source writer queue", bool(server.sendAsync(a, "q", 1)));
    }
    release.set_value();
    if (destinationWait) {
        require("writer callback waits for destination queue room", until([&] { return Access::waiting(server) == 1; }));
        check("waiting callback has not returned", !sent && Access::rejected(server) == array<uint64_t, 4>{});
    }
    releaseB.set_value();
    require("callback send and broadcast complete", done.wait_for(chrono::seconds(10)) == future_status::ready);
    check("callback send succeeds after destination room is available", sent == (freeCredits || destinationWait));
    const auto rejected = Access::rejected(server);
    check("callback rejection has exact total and first-loss Warning",
        freeCredits || destinationWait ? rejected == array<uint64_t, 4>{} && warnings.sends == 0 && queueErrors == 0
                                      : rejected[3] == 2 && warnings.sends == 1 && queueErrors == 2);
    check("callback leaves no admission waiter", Access::waiting(server) == 0);
    if (syncCompletion) check("original synchronous send completes", sender.get());
    server.stop();
    CLOSE_SOCKET(first); CLOSE_SOCKET(second);
    check("callback scenario returns every credit and worker", Access::bytes(server) == 0 && Access::workers(server) == 0);
}

void receiveCallbackWaits(CallbackPressure pressure) {
    TcpServer server;
    Warnings warnings;
    server.setTotalSendBufferSize(1);
    if (pressure == CallbackPressure::HighWater) server.setSendAsyncBufferSize(1);
    require("receive admission server starts", server.start(0, 1));
    auto fd = peer(server.getPort());
    require("receive admission peer registers", until([&] { return server.getClientCount() == 1; }));
    const int id = server.getClientIds()[0];
    promise<void> entered, release, returned;
    auto ready = entered.get_future(), done = returned.get_future();
    auto proceed = release.get_future().share();
    atomic<bool> gated{false}, sent{false};
    auto completion = server.onSendComplete.listen([&](TcpSendCompleteEventArgs&) {
        if (!gated.exchange(true)) { entered.set_value(); proceed.wait(); }
    });
    auto receive = server.onReceive.listen([&](TcpServerReceiveEventArgs& e) {
        sent = server.send(e.clientId, "x", 1);
        returned.set_value();
    });
    require("receive writer gate queued", bool(server.sendAsync(id, nullptr, 0)));
    require("receive writer gate entered", ready.wait_for(chrono::seconds(10)) == future_status::ready);
    if (pressure == CallbackPressure::Entries) {
        bool accepted = true;
        for (int i = 0; i < 4096; ++i) accepted &= bool(server.sendAsync(id, nullptr, 0));
        require("receive destination entries filled", accepted);
    } else {
        require("receive destination byte queued", bool(server.sendAsync(id, "q", 1)));
    }
    require("trigger receive callback", ::send(fd, "r", 1, 0) == 1);
    require("receive callback waits in admission", until([&] { return Access::waiting(server) == 1; }));
    check("receive wait does not reject", !sent && Access::rejected(server) == array<uint64_t, 4>{} && warnings.sends == 0);
    release.set_value();
    require("receive callback completes when room frees", done.wait_for(chrono::seconds(10)) == future_status::ready);
    check("receive synchronous send succeeds", sent);
    server.stop();
    CLOSE_SOCKET(fd);
    check("receive admission returns every credit and waiter", Access::bytes(server) == 0 && Access::waiting(server) == 0 && Access::workers(server) == 0);
}

void crossServerWriterAdmission() {
    TcpServer source, destination;
    Warnings warnings;
    destination.setTotalSendBufferSize(1);
    require("cross-server source starts", source.start(0, 1));
    require("cross-server destination starts", destination.start(0, 1));
    auto a = peer(source.getPort()), b = peer(destination.getPort());
    require("cross-server peers register", until([&] { return source.getClientCount() == 1 && destination.getClientCount() == 1; }));
    const int sourceId = source.getClientIds()[0], destinationId = destination.getClientIds()[0];
    promise<void> entered, release, returned;
    auto ready = entered.get_future(), done = returned.get_future();
    auto proceed = release.get_future().share();
    atomic<bool> gated{false}, handled{false}, nestedSent{true}, sent{true};
    auto gate = destination.onSendComplete.listen([&](TcpSendCompleteEventArgs&) {
        if (!gated.exchange(true)) { entered.set_value(); proceed.wait(); }
    });
    auto error = destination.onError.listen([&](TcpServerErrorEventArgs& e) {
        if (e.message == "Send queue full" && !handled.exchange(true))
            nestedSent = destination.send(destinationId, "n", 1);
    });
    auto completion = source.onSendComplete.listen([&](TcpSendCompleteEventArgs&) {
        sent = destination.send(destinationId, "x", 1);
        destination.broadcast("y", 1);
        returned.set_value();
    });
    require("cross-server destination gate queued", bool(destination.sendAsync(destinationId, nullptr, 0)));
    require("cross-server destination gate entered", ready.wait_for(chrono::seconds(10)) == future_status::ready);
    require("cross-server destination credit occupied", bool(destination.sendAsync(destinationId, "q", 1)));
    require("cross-server source callback queued", bool(source.sendAsync(sourceId, nullptr, 0)));
    require("cross-server and nested callbacks return", done.wait_for(chrono::seconds(10)) == future_status::ready);
    check("writer context survives cross-server and nested error calls", !sent && handled && !nestedSent &&
        Access::rejected(destination)[3] == 3 && warnings.sends == 1 && Access::waiting(destination) == 0);
    release.set_value();
    source.stop(); destination.stop();
    CLOSE_SOCKET(a); CLOSE_SOCKET(b);
    check("cross-server scenario releases credits", Access::bytes(source) == 0 && Access::bytes(destination) == 0);
}

void errorCallbackAdmission(bool queuedCredit) {
    TcpServer server;
    Warnings warnings;
    server.setTotalSendBufferSize(queuedCredit ? 2 : 1);
    require("error callback server starts", server.start(0, 2));
    auto first = peer(server.getPort()), second = peer(server.getPort());
    require("error callback peers register", until([&] { return server.getClientCount() == 2; }));
    const auto ids = server.getClientIds();
    const int a = ids[0], b = ids[1];
    promise<void> entered, release, returned;
    auto ready = entered.get_future(), done = returned.get_future();
    auto proceed = release.get_future().share();
    atomic<bool> gated{false}, handled{false}, sent{false}, released{false};
    auto completion = server.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (e.clientId == a && !gated.exchange(true)) { entered.set_value(); proceed.wait(); }
    });
    auto error = server.onError.listen([&](TcpServerErrorEventArgs& e) {
        if (e.clientId != a || handled.exchange(true)) return;
        released = Access::bytes(server) == size_t(queuedCredit) && Access::entries(server, a) == size_t(queuedCredit);
        sent = server.send(b, "x", 1);
        returned.set_value();
    });
    require("error writer gate queued", bool(server.sendAsync(a, nullptr, 0)));
    require("error writer gate entered", ready.wait_for(chrono::seconds(10)) == future_status::ready);
    require("shutdown send side forces real write failure", Access::failWrites(server, a));
    require("failing item admitted", bool(server.sendAsync(a, "f", 1)));
    if (queuedCredit) require("later item holds source writer credit", bool(server.sendAsync(a, "q", 1)));
    server.setTotalSendBufferSize(1);
    release.set_value();
    require("send-error callback returns without admission wait", done.wait_for(chrono::seconds(10)) == future_status::ready);
    check("failed item credits and entry released before onError", released);
    check("error callback sends when free and rejects when occupied", sent == !queuedCredit);
    check("error callback rejection counted and warned once", Access::rejected(server)[3] == uint64_t(queuedCredit) && warnings.sends == int(queuedCredit));
    server.stop();
    CLOSE_SOCKET(first); CLOSE_SOCKET(second);
    check("error scenario returns all credits", Access::bytes(server) == 0 && Access::waiting(server) == 0);
}

void disconnectedCompletionAdmission() {
    TcpServer server;
    Warnings warnings;
    server.setTotalSendBufferSize(1);
    require("disconnected completion server starts", server.start(0, 2));
    auto first = peer(server.getPort()), second = peer(server.getPort());
    require("disconnected completion peers register", until([&] { return server.getClientCount() == 2; }));
    const auto ids = server.getClientIds();
    const int a = ids[0], b = ids[1];
    promise<void> entered, release, returned;
    auto ready = entered.get_future(), done = returned.get_future();
    auto proceed = release.get_future().share();
    atomic<bool> gated{false}, handled{false}, sent{true};
    auto listener = server.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (e.clientId != a) return;
        if (!gated.exchange(true)) { entered.set_value(); proceed.wait(); return; }
        if (handled.exchange(true)) return;
        check("closed writer completes queued empty item as disconnected", e.error == SendError::Disconnected && e.bytesSent == 0);
        sent = server.send(b, "x", 1);
        returned.set_value();
    });
    require("disconnect gate queued", bool(server.sendAsync(a, nullptr, 0)));
    require("disconnect gate entered", ready.wait_for(chrono::seconds(10)) == future_status::ready);
    require("empty cancellation queued", bool(server.sendAsync(a, nullptr, 0)));
    require("later cancellation holds source credit", bool(server.sendAsync(a, "q", 1)));
    Access::closeChannel(server, a);
    release.set_value();
    require("disconnected completion callback returns", done.wait_for(chrono::seconds(10)) == future_status::ready);
    check("disconnected completion rejects and counts without waiting", !sent && Access::waiting(server) == 0 && Access::rejected(server)[3] == 1 && warnings.sends == 1);
    server.stop();
    CLOSE_SOCKET(first); CLOSE_SOCKET(second);
    check("disconnected completion returns all credits", Access::bytes(server) == 0);
}

void saturatedRejections() {
    TcpServer server;
    Warnings warnings;
    const auto maximum = (numeric_limits<uint64_t>::max)();
    // Issue each lifetime Warning before moving its total near overflow. A
    // counter-based once gate would emit a second Warning after wrapping.
    for (size_t reason = 0; reason < 4; ++reason) Access::rejectAtBound(server, reason);
    require("prime connection Warning server starts", server.start(0, 1));
    auto held = peer(server.getPort());
    require("prime connection slot occupied", until([&] { return server.getClientCount() == 1; }));
    auto excess = peer(server.getPort());
    char byte;
    require("prime connection rejected", ::recv(excess, &byte, 1, 0) <= 0);
    require("initial lifetime Warnings issued", until([&] { return warnings.connections == 1 && warnings.sends == 4; }));
    CLOSE_SOCKET(excess);
    server.stop();
    CLOSE_SOCKET(held);
    Access::seedRejections(server, maximum - 1);
    for (size_t reason = 0; reason < 4; ++reason) {
        for (int i = 0; i < 3; ++i) {
            check("seeded send is rejected at its real admission bound", Access::rejectAtBound(server, reason).error == SendError::QueueFull);
            check("send rejection total saturates", Access::rejected(server)[reason] == maximum);
        }
    }
    check("seeded send bounds each warn exactly once", warnings.sends == 4);
    for (int round = 0; round < 2; ++round) {
        require("saturation connection server starts", server.start(0, 1));
        auto held = peer(server.getPort());
        require("saturation connection occupies slot", until([&] { return server.getClientCount() == 1; }));
        for (int i = 0; i < 3; ++i) {
            auto excess = peer(server.getPort());
            char byte;
            require("saturated connection rejection closes socket", ::recv(excess, &byte, 1, 0) <= 0);
            CLOSE_SOCKET(excess);
            // Closing the rejected socket precedes accounting on the accept
            // thread. Wait for the first increment before testing saturation.
            if (round == 0 && i == 0)
                require("connection rejection reaches saturation", until([&] { return Access::connections(server) == maximum; }));
            check("connection rejection total saturates", Access::connections(server) == maximum);
        }
        require("seeded connection bound warns once", until([&] { return warnings.connections == 1; }));
        server.stop();
        CLOSE_SOCKET(held);
        for (size_t reason = 0; reason < 4; ++reason) Access::rejectAtBound(server, reason);
        check("saturated totals and once gates survive restart", warnings.sends == 4 && warnings.connections == 1 &&
            Access::rejected(server) == array<uint64_t, 4>{maximum, maximum, maximum, maximum});
    }
}
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
    Watchdog watchdog;
    const string scenario = argc > 1 ? argv[1] : "all";
    if (scenario == "all" || scenario == "exception" || scenario == "exception-async") {
        exceptionalAsyncReports(false);
        exceptionalAsyncReports(true);
    }
    if (scenario == "all" || scenario == "exception" || scenario == "exception-sync") {
        exceptionalSyncReports(false);
        exceptionalSyncReports(true);
    }
    if (scenario == "all" || scenario == "fanout" || scenario == "fanout-producer") fanoutWarningCanSend(false);
    if (scenario == "all" || scenario == "fanout" || scenario == "fanout-receive") fanoutWarningCanSend(true);
    if (scenario == "all" || scenario == "callback") {
        callbackAdmission(CallbackPressure::Aggregate);
        callbackAdmission(CallbackPressure::Aggregate, true);
        callbackAdmission(CallbackPressure::Entries);
        callbackAdmission(CallbackPressure::HighWater);
        callbackAdmission(CallbackPressure::Entries, false, true);
        callbackAdmission(CallbackPressure::HighWater, false, true);
        callbackAdmission(CallbackPressure::None);
        receiveCallbackWaits(CallbackPressure::Aggregate);
        receiveCallbackWaits(CallbackPressure::Entries);
        receiveCallbackWaits(CallbackPressure::HighWater);
        crossServerWriterAdmission();
        disconnectedCompletionAdmission();
    }
    if (scenario == "all" || scenario == "error") {
        errorCallbackAdmission(true);
        errorCallbackAdmission(false);
    }
    if (scenario == "all" || scenario == "saturation") saturatedRejections();
    if (scenario != "all") return failures ? 1 : 0;
    connectionBounds();
    sendCredits();
    broadcastAndSync();
    borrowedStream();
    rejectionListenerCanStop();
#ifndef _WIN32
    slowSockets();
#endif
    return failures ? 1 : 0;
}
