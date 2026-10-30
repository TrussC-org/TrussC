#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"
#include "tc/network/tcTcpServer.h"
#include <future>
#include <limits>

namespace trussc::internal {
struct TcpServerTestAccess {
    static auto state(TcpServer& server) { return server.state_; }
    static size_t workers(TcpServer& server) { return state(server)->workerClients_; }
    static uint64_t connections(TcpServer& server) { return state(server)->rejectedConnections_; }
    static size_t handles(TcpServer& server) {
        auto s = state(server);
        std::shared_lock<std::shared_mutex> lock(s->clientsMutex_);
        return s->clientThreads_.size() + s->clientWriters_.size() +
            s->finishedClientThreads_.size() + s->finishedWriters_.size();
    }
    static SendResult allocationFailure(TcpServer& server, int id) {
        auto s = state(server);
        TcpSendItem item;
        item.size = 1;
        return s->enqueue(s->findChannel(id), id, std::move(item),
            [](TcpSendItem&) { throw std::bad_alloc(); });
    }
    static void pendingOverflow() {
        TcpServer server;
        auto s = state(server);
        auto ch = std::make_shared<TcpSendChannel>();
        const auto maximum = (std::numeric_limits<size_t>::max)();
        s->sendAsyncBufferSize_ = maximum;
        ch->pendingBytes = maximum - 3;
        auto payload = std::make_shared<const std::vector<char>>(8, 'x');
        TcpSendItem item;
        item.size = payload->size();
        item.owned = payload;
        const auto rejected = s->enqueue(ch, 0, std::move(item));
        check("pending-byte overflow rejects owned async item without wrapping",
            rejected.error == SendError::QueueFull && rejected.id == 0 &&
            ch->pendingBytes == maximum - 3 && ch->queue.empty());
        // The same arithmetic applies to borrowed synchronous sends. They
        // must wait until the entire logical size can be represented.
        auto submit = [&] {
            TcpSendItem borrowed;
            borrowed.size = payload->size();
            borrowed.borrowed = payload->data();
            borrowed.waiter = std::make_shared<TcpSendWaiter>();
            return s->enqueue(ch, 0, std::move(borrowed));
        };
        auto waiting = std::async(std::launch::async, submit);
        check("sync enqueue waits for representable pending total",
            waiting.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
        {
            std::lock_guard<std::mutex> lock(ch->mutex);
            check("waiting sync item leaves accounting and queue unchanged",
                ch->pendingBytes == maximum - 3 && ch->queue.empty());
            ch->pendingBytes = maximum - 8;
        }
        ch->room.notify_all();
        check("sync enqueue resumes at exact representable boundary", bool(waiting.get()) &&
            ch->pendingBytes == maximum && ch->queue.size() == 1);
        auto cancelled = std::async(std::launch::async, submit);
        check("sync enqueue remains blocked at full pending total",
            cancelled.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
        { std::lock_guard<std::mutex> lock(ch->mutex); ch->open = false; }
        ch->room.notify_all();
        check("disconnect fails waiting sync enqueue without wrapping",
            cancelled.get().error == SendError::Disconnected && ch->pendingBytes == maximum && ch->queue.size() == 1);
    }
    static size_t entries(TcpServer& server, int id) {
        auto ch = state(server)->findChannel(id);
        std::lock_guard<std::mutex> lock(ch->mutex);
        return ch->queue.size();
    }
};
}
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
    atomic<int> connections{0};
    EventListener listener = getLogger().onLog.listen([this](LogEventArgs& e) {
        if (e.level != LogLevel::Warning) return;
        if (e.message.find("new connection closed") != string::npos) ++connections;
    });
};
void connectionBounds() {
    TcpServer server;
    server.setBindAddress("127.0.0.1");
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

void nowWorks() {
    TcpServer server;
    server.setBindAddress("127.0.0.1");
    require("server starts", server.start(0, 4));
    vector<TestSocket> peers;
    for (int i = 0; i < 4; ++i) peers.push_back(peer(server.getPort(), true));
    require("four slow readers connect", until([&] { return server.getClientCount() == 4; }));
    auto ids = server.getClientIds();
    // The optional per-client high-water mark remains. One large item can
    // cross it, so no item/aggregate setting is needed for this broadcast.
    vector<char> frame(17 * 1024 * 1024, 'f');
    auto failed = Access::allocationFailure(server, ids[0]);
    check("allocation failure returns a result without queuing", failed.error == SendError::AllocationFailed &&
        failed.id == 0 && server.getSendAsyncPendingBytes(ids[0]) == 0);
    // Hold writers at a completion barrier so queue contents do not depend
    // on how much data a platform's kernel accepts from a non-reading peer.
    promise<void> release;
    auto proceed = release.get_future().share();
    atomic<int> parked{0};
    auto completion = server.onSendComplete.listen([&](auto&) { ++parked; proceed.wait(); });
    require("marker queued to every slow reader", server.broadcastAsync("g") == 4);
    require("writers held before bulk queueing", until([&] { return parked == 4; }));
    require("broadcast over 64 MiB total accepts every recipient", server.broadcastAsync(frame) == 4);
    check("large owned item stays pending for slow peer", server.getSendAsyncPendingBytes(ids[0]) == frame.size());
    check("per-client high-water still rejects", server.sendAsync(ids[0], "x").error == SendError::QueueFull);
    server.setSendAsyncBufferSize(32 * 1024 * 1024);
    bool accepted = true;
    for (int i = 0; i < 5000; ++i) accepted &= bool(server.sendAsync(ids[0], "x"));
    check("5000 small sends above old entry cap accepted", accepted && Access::entries(server, ids[0]) >= 5000);
    release.set_value();
    server.stop();
    completion.disconnect();
    for (auto fd : peers) CLOSE_SOCKET(fd);

    require("restart for direct owned sends", server.start(0));
    auto fd = peer(server.getPort(), true);
    require("direct send peer connects", until([&] { return server.getClientCount() == 1; }));
    const int id = server.getClientIds()[0];
    check("copied sendAsync over 16 MiB succeeds", bool(server.sendAsync(id, frame.data(), frame.size())));
    server.setSendAsyncBufferSize(64 * 1024 * 1024);
    check("moved sendAsync over 16 MiB succeeds", bool(server.sendAsync(id, std::move(frame))));
    server.stop();
    CLOSE_SOCKET(fd);
}
}
TC_CORE_TEST_MAIN() {
    Watchdog watchdog;
    getMainThreadId();
    Access::pendingOverflow();
    connectionBounds();
    nowWorks();
    return failures ? 1 : 0;
}
