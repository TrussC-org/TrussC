#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"

namespace {
struct PausedWriterClient : ProbeClient {
    using TcpClient::stopConnectThread;
    mutex gateMutex;
    condition_variable gateCv;
    bool entered = false;
    bool released = false;

    int writeSendStep(const void* data, size_t size, bool& forWrite, int& error) override {
        {
            unique_lock<mutex> lock(gateMutex);
            entered = true;
            gateCv.notify_all();
            gateCv.wait(lock, [&] { return released; });
        }
        return TcpClient::writeSendStep(data, size, forWrite, error);
    }
    bool writerPaused() {
        lock_guard<mutex> lock(gateMutex);
        return entered;
    }
    void releaseWriter() {
        { lock_guard<mutex> lock(gateMutex); released = true; }
        gateCv.notify_all();
    }
};

void scenario() {
    // Keep an owned send active so the borrowed send stays in the queue.
    // Stopping the connect worker must remove queued bytes before waking the
    // sender, even while the writer cannot drain the closed channel.
    {
        RawListener listener(8, true);
        PausedWriterClient client;
        client.setSendTimeout(0);
        client.setSendAsyncBufferSize(0);
        atomic<int> completions{0};
        auto complete = client.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
            check("cancelled send reports disconnection", e.error == SendError::Disconnected);
            ++completions;
        });
        check("queued cancellation connect", client.connect("127.0.0.1", listener.port));
        TestSocket peer = listener.acceptPeer();
        const size_t activeBytes = 64 * 1024 * 1024;
        check("owned send queues before borrowed send", client.sendAsync(vector<char>(activeBytes, 'x')).ok());
        check("writer pauses on active owned bytes", until([&] { return client.writerPaused(); }));
        atomic<bool> returned{false}, failed{false};
        thread sender([&] {
            {
                string borrowed(4096, 'b');
                failed = !client.send(borrowed);
            } // The borrowed buffer is freed before returned is published.
            returned = true;
        });
        check("borrowed send stays queued", until([&] {
            return client.getSendAsyncPendingBytes() == activeBytes + 4096;
        }));
        check("another owned send queues", client.sendAsync(string("tail")).ok());
        client.stopConnectThread();
        check("queued borrowed sender returns failure", until([&] { return returned.load(); }) && failed);
        sender.join();
        check("cancelled queue bytes removed before writer resumes", client.getSendAsyncPendingBytes() == activeBytes);
        check("removed send completions wait for the writer", completions == 0);
        client.releaseWriter();
        client.disconnect();
        check("active send completes and pending bytes reach zero", completions == 3 && client.getSendAsyncPendingBytes() == 0);
        if (peer != INVALID_SOCKET) CLOSE_SOCKET(peer);
    }
    for (bool threads : {true, false}) for (bool async : {false, true}) {
        RawListener listener(8, true);
        BackpressureClient client;
        client.setUseThread(threads);
        client.setSendTimeout(0.5f);
        atomic<int> errors{0}, completions{0};
        atomic<bool> timedOut{false};
        atomic<SendError> outcome{SendError::None};
        auto err = client.onError.listen([&](TcpErrorEventArgs& e) {
            timedOut = e.message.find("timed out") != string::npos;
            ++errors;
        });
        auto complete = client.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
            outcome = e.error;
            ++completions;
        });
        check("connect to non-reading peer", client.connect("127.0.0.1", listener.port));
        check("connection completes", until([&] { if (!threads) client.processNetwork(); return client.isConnected(); }));
        TestSocket peer = listener.acceptPeer();
        check("peer accepted", peer != INVALID_SOCKET);
        client.smallSendBuffer();
        vector<char> data(64 * 1024 * 1024, 'x');
        if (async) {
            check("async idle send queues", client.sendAsync(std::move(data)).ok());
        } else {
            check("idle send fails", !client.send(data));
        }
        check("one completion", until([&] {
            if (!threads) client.processNetwork();
            return completions == 1;
        }));
        check("idle send reached socket back-pressure", client.wouldBlock &&
              client.acceptedBytes > 0 && client.acceptedBytes < 64 * 1024 * 1024);
        printf("native sends accepted %zu bytes before timeout\n", client.acceptedBytes.load());
        check("timeout reports once and disconnects", errors == 1 && timedOut && outcome == SendError::Disconnected && !client.isConnected());
        client.disconnect();
        if (peer != INVALID_SOCKET) CLOSE_SOCKET(peer);
    }
    for (bool threads : {true, false}) {
        RawListener listener(8, true);
        BackpressureClient client;
        client.setUseThread(threads);
        client.setSendTimeout(0);
        client.setSendAsyncBufferSize(1024);
        atomic<int> completions{0};
        atomic<SendError> outcome{SendError::None};
        auto complete = client.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
            outcome = e.error;
            ++completions;
        });
        check("async connect", client.connect("127.0.0.1", listener.port));
        check("async connection completes", until([&] { if (!threads) client.processNetwork(); return client.isConnected(); }));
        TestSocket peer = listener.acceptPeer();
        client.smallSendBuffer();
        auto queued = client.sendAsync(vector<char>(64 * 1024 * 1024, 'x'));
        check("oversized payload queues on an empty queue", queued.ok() && queued.id != 0);
        check("timeout-disabled send reaches socket back-pressure", until([&] {
            if (!threads) client.processNetwork();
            return client.wouldBlock.load();
        }));
        check("back-pressure refuses another payload", client.sendAsync(string("tail")).error == SendError::QueueFull);
        check("pending bytes exceed the mark", client.getSendAsyncPendingBytes() > client.getSendAsyncBufferSize());
        client.disconnect();
        check("disconnect drains completions with timeout disabled", completions == 1 && outcome == SendError::Disconnected);
        check("empty disconnected queue", client.getSendAsyncPendingBytes() == 0 && client.sendAsync(string("x")).error == SendError::NotRunning);
        if (peer != INVALID_SOCKET) CLOSE_SOCKET(peer);
    }
    // WebSocket owners may replace their client from a send-error listener.
    for (bool threads : {true, false}) {
        RawListener listener(8, true);
        auto owner = make_unique<BackpressureClient>();
        owner->setUseThread(threads);
        owner->setSendTimeout(0.5f);
        atomic<bool> destroyed{false};
        auto error = owner->onError.listen([&](TcpErrorEventArgs&) {
            owner.reset();
            destroyed = true;
        });
        check("send listener destruction: connect", owner->connect("127.0.0.1", listener.port));
        check("send listener destruction: connected", until([&] {
            if (!threads) owner->processNetwork();
            return owner->isConnected();
        }));
        TestSocket peer = listener.acceptPeer();
        owner->smallSendBuffer();
        ProbeClient* sender = owner.get();
        check("send listener destruction wakes the sender", !sender->send(vector<char>(64 * 1024 * 1024, 'x')));
        check("send listener destroyed the client", until([&] { return destroyed.load(); }));
        if (peer != INVALID_SOCKET) CLOSE_SOCKET(peer);
    }
    for (bool threads : {true, false}) {
        RawListener listener;
        TcpClient client;
        client.setUseThread(threads);
        client.setSendAsyncBufferSize(1);
        atomic<int> completions{0};
        auto complete = client.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
            check("successful completion", e.error == SendError::None && e.clientId == -1);
            ++completions;
        });
        check("ordered-send connect", client.connect("127.0.0.1", listener.port));
        check("ordered connection completes", until([&] { if (!threads) client.processNetwork(); return client.isConnected(); }));
        TestSocket peer = listener.acceptPeer();
        check("first async payload queued", client.sendAsync(string("A")).ok());
        check("sync payload sent", client.send(string("B")));
        check("last async payload queued", client.sendAsync(vector<char>{'C'}).ok());
        check("all sends complete once", until([&] {
            if (!threads) client.processNetwork();
            return completions == 3;
        }));
        string received;
        while (received.size() < 3) {
            char bytes[3];
            int count = static_cast<int>(recv(peer, bytes, sizeof(bytes), 0));
            if (count <= 0) break;
            received.append(bytes, static_cast<size_t>(count));
        }
        check("sync and async sends preserve order", received == "ABC");
        client.disconnect();
        CLOSE_SOCKET(peer);
    }
}
} // namespace

TC_CORE_TEST_MAIN() {
    TcpClient winsock; // Initialise process-wide Winsock before raw fixtures.
    Watchdog watchdog;
    scenario();
    return failures ? 1 : 0;
}
