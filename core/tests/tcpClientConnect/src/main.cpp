#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"

namespace {
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
            return client.wouldBlock && client.getSendAsyncPendingBytes() != 0;
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
        check("same target is a no-op", client.attempts == 1 && client.isConnecting());
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
    TcpClient winsock; // Initialise process-wide Winsock before raw fixtures.
    Watchdog watchdog;
    scenario();
    return failures ? 1 : 0;
}
