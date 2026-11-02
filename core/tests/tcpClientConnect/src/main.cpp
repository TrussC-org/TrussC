#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"
#include <future>
#include <limits>

namespace trussc::internal {
struct TcpClientTestAccess {
    static void pendingOverflow() {
        const auto maximum = (std::numeric_limits<size_t>::max)();
        for (size_t mark : {maximum, size_t(0)}) {
            TcpClient client;
            auto s = sharedLoad(client.state_);
            s->useThread_ = false;
            s->connected_ = true;
            s->sendAsyncBufferSize_ = mark;
            s->startSendChannel(); // No writer or valid socket: logical accounting only.
            const char bytes[8]{};
            auto seed = [&] {
                TcpSendItem item;
                item.borrowed = bytes;
                item.size = maximum - 3;
                return s->enqueue(std::move(item));
            };
            check("client pending-byte fixture seeded", bool(seed()));
            auto rejected = s->sendAsync(bytes, sizeof(bytes));
            check("client rejects owned overflow even with unlimited mark",
                rejected.error == SendError::QueueFull && rejected.id == 0 &&
                s->getSendAsyncPendingBytes() == maximum - 3);
            auto boundary = s->sendAsync(bytes, 3);
            check("client accepts exact representable boundary",
                bool(boundary) && s->getSendAsyncPendingBytes() == maximum);
            s->stopSendChannel(); // Cancels without reading the synthetic payload.
            s->connected_ = true;
            s->startSendChannel();
            check("client sync pending-byte fixture seeded", bool(seed()));
            // A non-threaded sync send tries to make room. Its invalid socket
            // fails that attempt, so the overflowing item must fail too.
            check("client sync send fails instead of wrapping pending total",
                !s->send(bytes, sizeof(bytes)));
            s->stopSendChannel();
            s->connected_ = false;
        }
    }
    static std::weak_ptr<void> state(const TcpClient& client) {
        return std::weak_ptr<void>(sharedLoad(client.state_));
    }
};
} // namespace trussc::internal

namespace {
void destroyWithOlderDeliveryHeld() {
    struct Fixture {
        promise<void> firstEntered, firstRelease, secondEntered, allowDestroy, destroyed;
        atomic<int> unexpected{0};
    };
    auto fixture = std::make_shared<Fixture>();
    auto firstEntered = fixture->firstEntered.get_future();
    auto firstRelease = fixture->firstRelease.get_future().share();
    auto secondEntered = fixture->secondEntered.get_future();
    auto allowDestroy = fixture->allowDestroy.get_future().share();
    auto destroyed = fixture->destroyed.get_future();
    RawListener first, second;
    auto client = make_unique<TcpClient>();
    auto oldState = internal::TcpClientTestAccess::state(*client);
    auto complete = client->onSendComplete.listen([&, fixture, firstRelease, allowDestroy](TcpSendCompleteEventArgs& e) {
        if (e.sendId == 1) {
            client->connectAsync("127.0.0.1", second.port);
            fixture->firstEntered.set_value();
            firstRelease.wait();
        } else if (e.sendId == 2) {
            fixture->secondEntered.set_value();
            allowDestroy.wait();
            client.reset();
            fixture->destroyed.set_value();
        } else ++fixture->unexpected;
    });
    check("older-delivery fixture connects", client->connect("127.0.0.1", first.port));
    // connect() replaces the initial idle State.
    oldState = internal::TcpClientTestAccess::state(*client);
    TestSocket peer = first.acceptPeer();
    check("older delivery queued", bool(client->sendAsync("a")));
    check("older delivery holds after reconnect", firstEntered.wait_for(chrono::seconds(10)) == future_status::ready);
    TestSocket next = second.acceptPeer();
    check("new run connects during older delivery", next != INVALID_SOCKET && until([&] { return client->isConnected(); }));
    auto currentState = internal::TcpClientTestAccess::state(*client);
    check("new delivery queued", bool(client->sendAsync("b")));
    check("new delivery enters", secondEntered.wait_for(chrono::seconds(10)) == future_status::ready);
    check("queued send awaits cancellation", bool(client->sendAsync("c")));
    fixture->allowDestroy.set_value();
    check("destroy from send delivery returns", destroyed.wait_for(chrono::seconds(10)) == future_status::ready);
    // The new run must drain its cancelled completion while the older run
    // still retains the shared delivery target. That target must not read a
    // destroyed Event even though its weak_ptr can still lock.
    check("new State finishes while older delivery is held", until([&] { return currentState.expired(); }));
    check("destroyed event receives no cancelled completion", fixture->unexpected == 0);
    fixture->firstRelease.set_value();
    check("older State finishes after delivery returns", until([&] { return oldState.expired(); }));
    if (peer != INVALID_SOCKET) CLOSE_SOCKET(peer);
    if (next != INVALID_SOCKET) CLOSE_SOCKET(next);
}

void receiveReconnectWithWriterHeld() {
    RawListener first, replacement;
    TcpClient client;
    promise<void> writerEntered, writerRelease, receiveReturned;
    auto entered = writerEntered.get_future();
    auto release = writerRelease.get_future().share();
    auto returned = receiveReturned.get_future();
    auto complete = client.onSendComplete.listen([&](TcpSendCompleteEventArgs&) {
        writerEntered.set_value();
        release.wait();
    });
    auto receive = client.onReceive.listen([&](TcpReceiveEventArgs&) {
        client.connectAsync("127.0.0.1", replacement.port);
        receiveReturned.set_value();
    });
    check("held-writer fixture connects", client.connect("127.0.0.1", first.port));
    TestSocket peer = first.acceptPeer();
    check("held-writer payload queued", bool(client.sendAsync("x")));
    check("writer listener enters", entered.wait_for(chrono::seconds(10)) == future_status::ready);
    check("held-writer receive trigger", ::send(peer, "r", 1, 0) == 1);
    check("receive reconnect returns while writer listener is held",
          returned.wait_for(chrono::seconds(10)) == future_status::ready);
    writerRelease.set_value();
    TestSocket next = replacement.acceptPeer();
    check("replacement connects with independent State", next != INVALID_SOCKET && until([&] { return client.isConnected(); }));
    client.disconnect();
    if (peer != INVALID_SOCKET) CLOSE_SOCKET(peer);
    if (next != INVALID_SOCKET) CLOSE_SOCKET(next);
}

void receiveRestartAndMove() {
    for (bool threaded : {true, false}) {
        RawListener first, second;
        TcpClient client;
        client.setUseThread(threaded);
        atomic<bool> restarted{false};
        auto receive = client.onReceive.listen([&](TcpReceiveEventArgs&) {
            client.connectAsync("127.0.0.1", second.port);
            restarted = true;
        });
        check("connect receive-restart fixture", client.connect("127.0.0.1", first.port));
        TestSocket peer = first.acceptPeer();
        check("receive-restart peer accepted", peer != INVALID_SOCKET);
        check("send receive trigger", ::send(peer, "x", 1, 0) == 1);
        check("connectAsync from receive returns", until([&] {
            if (!threaded) client.processNetwork();
            return restarted.load();
        }));
        TestSocket replacement = second.acceptPeer();
        check("new attempt has independent connection", until([&] {
            if (!threaded) client.processNetwork();
            return client.isConnected();
        }) && replacement != INVALID_SOCKET && client.getRemotePort() == second.port);
        int moveErrors = 0;
        auto log = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Error && e.message == "[TcpClient] moved while running; stopped") ++moveErrors;
        });
        TcpClient moved(std::move(client));
        check("TCP connected move stops with one Error", moveErrors == 1 && !moved.isConnected() && !client.isConnected());
        RawListener movedPeer;
        atomic<int> movedReceives{0};
        auto movedListener = moved.onReceive.listen([&](TcpReceiveEventArgs&) { ++movedReceives; });
        check("moved TCP handle can reconnect", moved.connect("127.0.0.1", movedPeer.port));
        TestSocket movedConnection = movedPeer.acceptPeer();
        check("send to moved TCP handle", ::send(movedConnection, "x", 1, 0) == 1);
        check("TCP listeners stay on original handle", until([&] {
            if (!threaded) moved.processNetwork();
            return movedReceives == 1;
        }) && moved.getRemotePort() == movedPeer.port);
        moved.disconnect();
        if (movedConnection != INVALID_SOCKET) CLOSE_SOCKET(movedConnection);
        if (peer != INVALID_SOCKET) CLOSE_SOCKET(peer);
        if (replacement != INVALID_SOCKET) CLOSE_SOCKET(replacement);
    }
    RawListener peerListener;
    auto client = make_unique<TcpClient>();
    atomic<bool> destroyed{false};
    auto connect = client->onConnect.listen([&](TcpConnectEventArgs& e) {
        if (e.success) { client.reset(); destroyed = true; }
    });
    client->connectAsync("127.0.0.1", peerListener.port);
    TestSocket peer = peerListener.acceptPeer();
    check("destroy on connect thread returns safely", until([&] { return destroyed.load(); }));
    if (peer != INVALID_SOCKET) CLOSE_SOCKET(peer);
}

void scenario() {
    // Destruction can win before the connect worker has even entered connect().
    RawListener listener;
    for (int i = 0; i < 100; ++i) {
        TcpClient client;
        client.connectAsync("127.0.0.1", listener.port);
    }
    check("immediate destruction after connectAsync completes", true);
    // A connect listener can itself be blocked in a synchronous send. Joining
    // the connect worker must also cancel that queue, even with no send limit.
    for (bool cancel : {true, false}) {
        RawListener peerListener(8, true);
        BackpressureClient client;
        client.setSendTimeout(cancel ? 0.0f : 0.5f);
        atomic<bool> sent{false}, failed{false}, errorHandled{false};
        auto connect = client.onConnect.listen([&](TcpConnectEventArgs& e) {
            if (!e.success) return;
            client.smallSendBuffer();
            failed = !client.send(vector<char>(64 * 1024 * 1024, 'x'));
            sent = true;
        });
        auto error = client.onError.listen([&](TcpErrorEventArgs&) {
            client.disconnect();
            errorHandled = true;
        });
        client.connectAsync("127.0.0.1", peerListener.port);
        TestSocket peer = peerListener.acceptPeer();
        check("connect listener peer accepted", peer != INVALID_SOCKET);
        check("connect listener has a queued send", until([&] {
            return client.pressure->wouldBlock && client.getSendAsyncPendingBytes() != 0;
        }));
        if (cancel) client.disconnect();
        check("connect listener send is released", until([&] { return sent.load(); }) && failed);
        if (!cancel) check("send error can disconnect while joining connect", until([&] { return errorHandled.load(); }));
        client.disconnect();
        check("connect listener leaves no connection", !client.isConnected() && !client.isConnecting());
        if (peer != INVALID_SOCKET) CLOSE_SOCKET(peer);
    }

#ifdef __linux__
    // Linux drops SYNs once this accept queue (backlog 0 -> one entry) is full.
    RawListener full(0);
    TestSocket filler = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(full.port));
    check("fill accept queue", ::connect(filler, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    {
        ProbeClient client;
        client.connectAsync("127.0.0.1", full.port);
        check("async connect is visible before worker starts", client.isConnecting());
        check("TCP attempt is pending", until([&] { return client.pending(); }));
        client.connectAsync("127.0.0.1", full.port);
        check("same target is a no-op", client.probe->attempts == 1 && client.isConnecting());
        RawListener next;
        client.connectAsync("127.0.0.1", next.port);
        check("different target connects", until([&] { return client.isConnected(); }));
        TestSocket peer = next.acceptPeer();
        check("new target accepts", peer != INVALID_SOCKET && client.getRemotePort() == next.port);
        client.disconnect();
        if (peer != INVALID_SOCKET) CLOSE_SOCKET(peer);
    }
    for (bool threads : {true, false}) {
        ProbeClient client;
        client.setUseThread(threads);
        client.setConnectTimeout(0.5f);
        atomic<int> errors{0}, failed{0};
        auto error = client.onError.listen([&](TcpErrorEventArgs&) { ++errors; });
        auto connect = client.onConnect.listen([&](TcpConnectEventArgs& e) { if (!e.success) ++failed; });
        client.connectAsync("127.0.0.1", full.port);
        check("configured connect timeout reports failure", until([&] {
            if (!threads) client.processNetwork();
            return failed == 1;
        }));
        client.disconnect();
        check("one timeout and no connection", failed == 1 && errors == 1 && !client.isConnected() && !client.isConnecting());
    }
    {
        ProbeClient client;
        client.setConnectTimeout(0);
        client.connectAsync("127.0.0.1", full.port);
        check("no-deadline attempt reaches TCP wait", until([&] { return client.pending(); }));
        client.disconnect();
        check("disconnect cancels TCP wait", !client.isConnected() && !client.isConnecting());
    }
    {
        auto client = make_unique<ProbeClient>();
        client->connectAsync("127.0.0.1", full.port);
        check("destructor test reaches TCP wait", until([&] { return client->pending(); }));
        client.reset();
        check("destructor cancels TCP wait", true);
    }
    CLOSE_SOCKET(filler);
#else
    printf("full accept queue scenarios: SKIP (Linux fixture)\n");
#endif
}
} // namespace

TC_CORE_TEST_MAIN() {
    internal::TcpClientTestAccess::pendingOverflow();
    TcpClient winsock; // Initialise process-wide Winsock before raw fixtures.
    Watchdog watchdog;
    destroyWithOlderDeliveryHeld();
    receiveReconnectWithWriterHeld();
    receiveRestartAndMove();
    scenario();
    return failures ? 1 : 0;
}
