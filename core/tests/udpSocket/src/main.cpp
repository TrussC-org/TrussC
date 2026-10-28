#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"
#include "tc/network/tcUdpSocket.h"
#include "tc/events/tcCoreEvents.h"
#include "tc/utils/tcLog.h"
#ifndef _WIN32
#include <fcntl.h>
#endif

namespace trussc::internal {
struct UdpSocketLifecycleTestAccess {
    static SocketHandle descriptor(const UdpSocket& socket) {
        return internal::sharedLoad(socket.state_)->socket_->handle.load();
    }
};
}

namespace {
void preservesSocket(bool threaded) {
    UdpSocket socket, sink;
    socket.setBindAddress("127.0.0.1");
    sink.setBindAddress("127.0.0.1");
    socket.setUseThread(threaded);
    check("bind option preservation fixture", socket.bind(0, false) && sink.bind(0, false));
    const int port = socket.getLocalPort();
    check("set broadcast before receive run", socket.setBroadcast(true));
    const bool multicast = socket.joinMulticastGroup("239.255.79.6", "127.0.0.1");
    if (!multicast) printf("SKIP: loopback multicast unavailable; broadcast still checked\n");
    check("connect UDP send destination", socket.connect("127.0.0.1", sink.getLocalPort()));
    check("set sink receive deadline", sink.setReceiveTimeout(2000));
    auto sentAndReceived = [&] {
        if (!socket.send("retained")) return false;
        char data[16]{};
        string host;
        int sourcePort = 0;
        return sink.receive(data, sizeof(data), host, sourcePort) == 8 &&
               string(data, 8) == "retained" && sourcePort == port;
    };
    socket.startReceiving();
    socket.stopReceiving();
    check("stop keeps socket and bind(0) port", socket.isValid() && socket.getLocalPort() == port);
    check("connected send works while stopped", sentAndReceived());
#ifndef _WIN32
    const int flags = fcntl(internal::UdpSocketLifecycleTestAccess::descriptor(socket), F_GETFL, 0);
    check("stop restores direct receive mode", flags >= 0 && bool(flags & O_NONBLOCK) == !threaded);
#endif
    socket.startReceiving();
    int broadcast = 0;
#ifdef _WIN32
    int size = sizeof(broadcast);
#else
    socklen_t size = sizeof(broadcast);
#endif
    check("broadcast option survives stop/start", getsockopt(
        internal::UdpSocketLifecycleTestAccess::descriptor(socket), SOL_SOCKET, SO_BROADCAST,
        reinterpret_cast<char*>(&broadcast), &size) == 0 && broadcast != 0);
    check("bound port survives stop/start", socket.getLocalPort() == port);
    check("connected send works after restart", sentAndReceived());
    if (multicast) check("multicast membership survives stop/start",
        socket.leaveMulticastGroup("239.255.79.6", "127.0.0.1"));
    socket.stopReceiving();
    socket.close();
    check("close drops the socket", !socket.isValid() && socket.getLocalPort() == 0);
}
void lifecycle(bool threaded) {
    UdpSocket sender;
    auto socket = make_unique<UdpSocket>();
    socket->setBindAddress("127.0.0.1");
    socket->setUseThread(threaded);
    check("bind UDP loopback", socket->bind(0));
    const int port = socket->getLocalPort();
    atomic<int> packets{0};
    atomic<bool> restarted{false}, destroyed{false};
    auto listener = socket->onReceive.listen([&](UdpReceiveEventArgs&) {
        const int packet = ++packets;
        if (packet == 1) {
            socket->stopReceiving();
            check("stop inside receive preserves the socket", !socket->isReceiving() && socket->isValid());
            socket->startReceiving();
            restarted = true;
        } else {
            socket.reset();
            destroyed = true;
        }
    });
    check("send first datagram", sender.sendTo("127.0.0.1", port, "first"));
    check("restart inside listener", until([&] {
        if (!threaded && socket) events().update.notify();
        return restarted.load();
    }));
    // A datagram already queued for the restarted run cannot touch the old buffer.
    check("send to restarted port", sender.sendTo("127.0.0.1", port, "second"));
    check("destroy inside receive listener", until([&] {
        if (!threaded && socket) events().update.notify();
        return destroyed.load();
    }) && packets == 2);
}
void moves(bool threaded) {
    UdpSocket sender, original;
    original.setBindAddress("127.0.0.1");
    original.setUseThread(threaded);
    check("bind stopped UDP socket", original.bind(0, false));
    const int port = original.getLocalPort();
    int errors = 0;
    auto logs = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message == "[UdpSocket] moved while running; stopped") ++errors;
    });
    atomic<int> oldPackets{0}, newPackets{0};
    auto oldListener = original.onReceive.listen([&](UdpReceiveEventArgs&) { ++oldPackets; });
    UdpSocket moved(std::move(original));
    check("stopped move transfers socket and port", moved.isValid() && moved.getLocalPort() == port && !original.isValid() && errors == 0);
    auto newListener = moved.onReceive.listen([&](UdpReceiveEventArgs&) { ++newPackets; });
    moved.startReceiving();
    check("send after stopped move", sender.sendTo("127.0.0.1", port, "x"));
    check("listeners stay with stopped source", until([&] {
        if (!threaded) moved.processNetwork();
        return newPackets == 1;
    }) && oldPackets == 0);
    UdpSocket finalSocket(std::move(moved));
    check("running move stops with one Error", !finalSocket.isReceiving() && !moved.isReceiving() && errors == 1);
    atomic<int> finalPackets{0};
    auto finalListener = finalSocket.onReceive.listen([&](UdpReceiveEventArgs&) { ++finalPackets; });
    finalSocket.startReceiving();
    check("restart moved socket", sender.sendTo("127.0.0.1", port, "x"));
    check("listeners stay with running source", until([&] {
        if (!threaded) finalSocket.processNetwork();
        return finalPackets == 1;
    }) && newPackets == 1);
    finalSocket.stopReceiving();
}
void restartWithCallbackInFlight() {
    UdpSocket receiver, sender;
    receiver.setBindAddress("127.0.0.1");
    promise<void> ready, release, oldReturned, delivered;
    auto gate = release.get_future().share();
    auto newReady = ready.get_future();
    auto oldDone = oldReturned.get_future();
    auto newDone = delivered.get_future();
    atomic<int> packets{0};
    check("bind in-flight UDP fixture", receiver.bind(0));
    const int port = receiver.getLocalPort();
    auto listener = receiver.onReceive.listen([&](UdpReceiveEventArgs& e) {
        if (++packets == 1) {
            receiver.stopReceiving();
            receiver.startReceiving();
            ready.set_value();
            gate.wait();
            check("old callback retains its datagram", string(e.data.begin(), e.data.end()) == "old");
            oldReturned.set_value();
        } else {
            check("new run receives independently", string(e.data.begin(), e.data.end()) == "new");
            delivered.set_value();
        }
    });
    check("queue old datagram", sender.sendTo("127.0.0.1", port, "old"));
    check("new run starts before old callback returns", newReady.wait_for(chrono::seconds(10)) == future_status::ready);
    check("queue in-flight replacement datagram", sender.sendTo("127.0.0.1", port, "new"));
    check("new datagram delivered while old callback held", newDone.wait_for(chrono::seconds(10)) == future_status::ready);
    release.set_value();
    check("old callback returns", oldDone.wait_for(chrono::seconds(10)) == future_status::ready);
    receiver.stopReceiving();
}
void moveInsideListener() {
    UdpSocket source, sender;
    source.setBindAddress("127.0.0.1");
    unique_ptr<UdpSocket> destination;
    atomic<bool> done{false};
    check("bind self-move fixture", source.bind(0));
    auto listener = source.onReceive.listen([&](UdpReceiveEventArgs&) {
        destination = make_unique<UdpSocket>(std::move(source));
        done = true;
    });
    check("send self-move datagram", sender.sendTo("127.0.0.1", source.getLocalPort(), "x"));
    check("move on receive thread returns stopped", until([&] { return done.load(); }) && destination && !destination->isReceiving());
}
// close() from another thread must wake a thread blocked in a direct receive()
// on a bound, blocking socket (that thread keeps the run's State, and so the OS
// socket, alive; only shutdown() makes recvfrom return).
void closeWakesBlockedReceive() {
    UdpSocket socket;
    socket.setBindAddress("127.0.0.1");
    check("bind blocking receive fixture", socket.bind(0, false));
    promise<int> received;
    auto result = received.get_future();
    atomic<bool> entering{false};
    thread reader([&] {
        char buffer[64];
        entering = true;
        received.set_value(socket.receive(buffer, sizeof(buffer)));
    });
    check("reader is about to receive", until([&] { return entering.load(); }));
    socket.close();
    const bool woke = result.wait_for(chrono::seconds(10)) == future_status::ready;
    check("close wakes a blocked receive()", woke);
    if (!woke) _Exit(3);   // a reader still in recvfrom would keep the test hanging
    reader.join();
    check("blocked receive() returns no data after close", result.get() <= 0);
}
} // namespace
TC_CORE_TEST_MAIN() {
    Watchdog watchdog;
    for (bool threaded : {true, false}) { preservesSocket(threaded); lifecycle(threaded); moves(threaded); }
    restartWithCallbackInFlight();
    moveInsideListener();
    closeWakesBlockedReceive();
    return failures ? 1 : 0;
}
