#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"
#include "tc/network/tcTcpServer.h"
#include <future>
#include "../../../../addons/tcxOsc/src/tcxOscReceiver.h"
#include "../../../../addons/tcxMidi/src/tcxMidiIn.h"
#include "../../../../addons/tcxWebSocket/src/tcWebSocketClient.h"

namespace trussc::internal {
struct ExternalUdpDeliveryTestAccess {
    static void receiveError(UdpSocket& socket) {
        socket.notifyError(UdpSocket::ErrorKind::Receive, "Receive error", 42);
    }
    static void appError(UdpSocket& socket) {
        socket.notifyError(UdpSocket::ErrorKind::Send, "Application operation failed", 43);
    }
};
}

namespace tcx::osc {
struct OscDeliveryTestAccess {
    static void prepare(OscReceiver& receiver) { receiver.prepareSocket(0); }
    static void error(OscReceiver& receiver, bool external) {
        if (external) tc::internal::ExternalUdpDeliveryTestAccess::receiveError(receiver.socket_);
        else tc::internal::ExternalUdpDeliveryTestAccess::appError(receiver.socket_);
    }
    static void receive(OscReceiver& receiver, tc::UdpReceiveEventArgs& packet) {
        receiver.state_->handleReceive(packet, receiver.state_->generation_);
    }
};
}

namespace {
using Store = internal::ExternalDeliveryStore;
template<class T> using External = internal::ExternalEventDelivery<T>;

struct CopyProbe {
    int value = 7;
    static std::atomic<int> copies;
    CopyProbe() = default;
    CopyProbe(const CopyProbe& other) : value(other.value) { ++copies; }
};
std::atomic<int> CopyProbe::copies{0};

struct ClosureProbe {
    static std::atomic<int> copies;
    ClosureProbe() = default;
    ClosureProbe(const ClosureProbe&) { ++copies; }
};
std::atomic<int> ClosureProbe::copies{0};

void countAndFanout() {
    Event<CopyProbe> source;
    int called = 0;
    bool checkedExecuting = false;
    auto first = source.listen([&, tracked = ClosureProbe{}](CopyProbe& p) {
        (void)tracked;
        if (!checkedExecuting) {
            checkedExecuting = true;
            std::thread producer([&] { CopyProbe value; check("executing copies retain credits", !External<CopyProbe>::notify(source, value)); });
            producer.join();
            const int captureCopies = ClosureProbe::copies;
            std::thread registrar([&] {
                auto temporary = source.listen([](CopyProbe&) {}, Deliver::Main);
            });
            registrar.join();
            check("listener updates do not copy an executing callback", ClosureProbe::copies == captureCopies);
        }
        check("independent payload", p.value == 7); p.value = 1; ++called;
    }, Deliver::Main);
    auto second = source.listen([&](CopyProbe& p) { check("second independent payload", p.value == 7); ++called; }, Deliver::Main);
    CopyProbe payload;
    CopyProbe::copies = 0;
    ClosureProbe::copies = 0;
    std::thread worker([&] {
        for (int i = 0; i < 512; ++i) check("fanout admission", External<CopyProbe>::notify(source, payload));
        for (int i = 0; i < 101; ++i)
            check("both Main copies rejected", !External<CopyProbe>::notify(source, payload));
    });
    worker.join();
    auto store = External<CopyProbe>::diagnostics(source);
    check("1024 copies pending before any drain", store && store->pending == 1024 && called == 0);
    check("rejection occurs before payload copying", CopyProbe::copies == 1024);
    check("only admitted notifications copy callable captures", ClosureProbe::copies == 512);
    check("every rejected Main copy counted", store->rejectedCopies == 202);
    internal::drainMainThreadQueue();
    check("all admitted listeners run", called == 1024);
    check("repeated losses schedule only one Warning job", internal::getMainThreadQueuePending() == 1025);
    check("credits returned", store->pending == 0 && store->retainedBytes == 0);
    check("rejections remain cumulative after drain", store->rejectedCopies == 204);
}

void bytesAndUnits() {
    Event<tcx::websocket::WebSocketEventArgs> source;
    tcx::websocket::WebSocketEventArgs payload;
    payload.data.resize(1024 * 1024);
    payload.message.assign(1024 * 1024, 'w');
    const size_t charge = internal::ExternalPayload<tcx::websocket::WebSocketEventArgs>::bytes(payload);
    const size_t admittedCopies = Store::byteLimit / charge;
    const size_t admitted = admittedCopies / 2;
    int calls = 0;
    auto first = source.listen([&](tcx::websocket::WebSocketEventArgs& p) { check("whole WebSocket payload", p.data.size() == payload.data.size() && p.message == payload.message); ++calls; }, Deliver::Main);
    auto second = source.listen([&](tcx::websocket::WebSocketEventArgs&) { ++calls; }, Deliver::Main);
    auto gate = std::make_shared<internal::NetworkDelivery>();
    auto target = internal::EventSnapshotDelivery<tcx::websocket::WebSocketEventArgs>::bind(source, gate);
    internal::EventSnapshotDelivery<tcx::websocket::WebSocketEventArgs> ingress{target};
    std::thread worker([&] {
        for (size_t i = 0; i < admitted; ++i) check("byte admission", ingress.notifyExternal(payload));
        check("aggregate fanout byte overflow rejects whole copies", !ingress.notifyExternal(payload));
    });
    worker.join();
    auto store = External<tcx::websocket::WebSocketEventArgs>::diagnostics(source);
    check("aggregate retained payload includes both representations and listeners", store->retainedBytes == admittedCopies * charge && store->retainedBytes <= Store::byteLimit);
    check("byte rejection before count bound", store->pending < 1024 && store->rejectedCopies == 2 - admittedCopies % 2);
    internal::drainMainThreadQueue();
    check("only whole admitted copies run", calls == static_cast<int>(admittedCopies));

    Event<std::string> oversized;
    int rejectedCalls = 0;
    auto listener = oversized.listen([&](std::string&) { ++rejectedCalls; }, Deliver::Main);
    std::string large(Store::byteLimit + 1, 'x');
    std::thread producer([&] { check("single oversized event rejected", !External<std::string>::notify(oversized, large)); });
    producer.join();
    internal::drainMainThreadQueue();
    check("oversized event has no prefix delivery", rejectedCalls == 0);
}

void oscAndMidi() {
    tcx::osc::OscReceiver osc;
    tcx::osc::OscMessage message("/bounded");
    message.addString("complete");
    const auto bytes = message.toBytes();
    UdpReceiveEventArgs packet;
    packet.data.assign(bytes.begin(), bytes.end());
    int oscCalls = 0;
    auto oscListener = osc.onMessageReceived.listen([&](tcx::osc::OscMessage& p) {
        check("whole OSC message", p.getArgAsString(0) == "complete"); ++oscCalls;
    }, Deliver::Main);
    tcx::midi::MidiIn midi;
    int midiCalls = 0;
    auto midiListener = midi.onMessage.listen([&](tcx::midi::MidiMessage& p) {
        check("whole MIDI message", p.bytes == std::vector<unsigned char>({0x90, 60, 100})); ++midiCalls;
    }, Deliver::Main);
    check("open MIDI mock", midi.openPort(0));
    auto callback = libremidi::test::inputCallback;
    std::thread worker([&] {
        for (int i = 0; i < 1025; ++i) {
            tcx::osc::OscDeliveryTestAccess::receive(osc, packet);
            callback(libremidi::message{{0x90, 60, 100}, 0});
        }
    });
    worker.join();
    auto oscStore = External<tcx::osc::OscMessage>::diagnostics(osc.onMessageReceived);
    auto midiStore = External<tcx::midi::MidiMessage>::diagnostics(midi.onMessage);
    check("OSC stalled Main with polling never enabled", oscCalls == 0 && oscStore->pending == 1024 && oscStore->rejectedCopies == 1);
    check("MIDI stalled Main with polling never enabled", midiCalls == 0 && midiStore->pending == 1024 && midiStore->rejectedCopies == 1);
    internal::drainMainThreadQueue();
    check("OSC/MIDI whole-unit drop", oscCalls == 1024 && midiCalls == 1024);
    midi.closePort();
    libremidi::test::inputCallback = {};

    tcx::osc::OscMessage blob("/blob");
    std::vector<uint8_t> data(1024, 1);
    blob.addBlob(data.data(), data.size());
    tcx::osc::OscBundle child, tree;
    child.addMessage(blob);
    tree.addBundle(child);
    check("OSC nested allocations charged", internal::ExternalPayload<tcx::osc::OscBundle>::bytes(tree) > 1024 + sizeof(tree) + sizeof(child) + sizeof(blob));
}

// Two targets share the production acquisition gate. Removing the final
// capture reenters the other target while external acquisition is in flight.
// The suite watchdog turns a lock-order regression into a failing process.
template<class T> void sharedGateRemoval(bool clear) {
    using Delivery = internal::EventSnapshotDelivery<T>;
    Event<T> source, other;
    auto gate = std::make_shared<internal::NetworkDelivery>();
    auto target = Delivery::bind(source, gate);
    auto otherTarget = Delivery::bind(other, gate);
    std::promise<void> start;
    auto go = start.get_future();
    std::atomic<bool> done{false};
    struct Capture {
        std::shared_ptr<internal::NetworkDelivery> gate;
        std::shared_ptr<typename Delivery::Target> other;
        std::promise<void>& start;
        std::atomic<bool>& done;
        ~Capture() {
            start.set_value();
            while (!done.load()) {
                if (!gate->mutex.try_lock()) break;
                gate->mutex.unlock();
                std::this_thread::yield();
            }
            if constexpr (std::is_void_v<T>) (*other)();
            else { T arg{}; (*other)(arg); }
        }
    };
    auto capture = std::shared_ptr<Capture>(new Capture{gate, otherTarget, start, done});
    auto removed = source.listen([capture](auto&...) {});
    auto remaining = source.listen([](auto&...) {}, Deliver::Main);
    capture.reset();
    std::thread worker([&] {
        go.wait();
        if constexpr (std::is_void_v<T>) target->dispatch(true);
        else { T arg{}; target->dispatch(arg, {}, true); }
        done = true;
    });
    if (clear) source.clear();
    else removed.disconnect();
    worker.join();
    check("shared-gate external acquisition and capture destruction finish", done);
    internal::drainMainThreadQueue();
}

void lockOrder() {
    sharedGateRemoval<int>(false);
    sharedGateRemoval<void>(false);
    sharedGateRemoval<int>(true);
    sharedGateRemoval<void>(true);
}

void oscForwardedErrors() {
    tcx::osc::OscReceiver receiver;
    // Install the real socket forwarding listeners. Error injection below
    // exercises UDP classification without depending on a failing OS socket.
    tcx::osc::OscDeliveryTestAccess::prepare(receiver);
    int calls = 0, appCalls = 0, warnings = 0;
    auto logger = getLogger().onLog.listen([&](LogEventArgs& args) {
        if (args.level == LogLevel::Warning && args.message.find("[ExternalDelivery]") != std::string::npos) {
            check("forwarded loss Warning runs on main", isMainThread());
            ++warnings;
        }
    });
    auto listener = receiver.onParseError.listen([&](std::string& msg) {
        ++calls;
        if (msg.find("Application operation") != std::string::npos) ++appCalls;
    }, Deliver::Main);
    std::thread worker([&] {
        for (int i = 0; i < 10000; ++i) tcx::osc::OscDeliveryTestAccess::error(receiver, true);
    });
    worker.join();
    auto store = External<std::string>::diagnostics(receiver.onParseError);
    check("forwarded receive errors bound pending copies and count every rejection",
          store && store->pending == 1024 && store->rejectedCopies == 8976);
#if !defined(__EMSCRIPTEN__)
    check("forwarded receive errors queue one Warning alongside admitted copies",
          internal::mainThreadQueue().size() == 1025);
#endif
    std::thread app([&] {
        for (int i = 0; i < 10000; ++i) tcx::osc::OscDeliveryTestAccess::error(receiver, false);
    });
    app.join();
    check("forwarded app errors bypass saturated external store",
          store && store->pending == 1024 && store->rejectedCopies == 8976);
#if !defined(__EMSCRIPTEN__)
    check("forwarded app error copies stay growable", internal::mainThreadQueue().size() == 11025);
#endif
    internal::drainMainThreadQueue();
    check("forwarded receive/app error callbacks remain whole", calls == 11024 && appCalls == 10000);
    check("forwarded receive loss warns exactly once without polling", warnings == 1);
    check("forwarded error credits released after drain", store && store->pending == 0 && store->retainedBytes == 0);
}

struct ReentrantDeliveryClient : TcpClient {
    struct ConnectionTransport : Transport {
        bool connectSocket(State& st, const std::string&, int) override {
            st.running_ = true;
            return true;
        }
    };
    ReentrantDeliveryClient() { setTransport(std::make_shared<ConnectionTransport>()); setUseThread(false); }
    bool receive(TcpReceiveEventArgs& arg) { auto run = state(); return run->deliverReceive(arg); }
};

// Overflow teardown completes the pending send; its Inline completion
// listener reconnects before the overflow disconnect would be emitted.
void overflowCompletionReconnect() {
    ReentrantDeliveryClient client;
    int completions = 0, unclean = 0, staleUnclean = 0;
    bool reconnected = false;
    auto main = client.onReceive.listen([](auto&) {}, Deliver::Main);
    auto completion = client.onSendComplete.listen([&](auto&) {
        if (++completions == 1) reconnected = client.connect("replacement", 2);
    });
    auto close = client.onDisconnect.listen([&](auto& args) {
        if (!args.wasClean) {
            ++unclean;
            if (client.isConnected()) ++staleUnclean;
        }
    });
    check("completion test transport connected", client.connect("initial", 1));
    auto pending = client.sendAsync("pending send");
    check("completion test send pending", static_cast<int>(pending.error) == 0);
    std::thread worker([&] {
        TcpReceiveEventArgs args; args.data = {'a'};
        for (int i = 0; i < 1024; ++i) client.receive(args);
        check("completion test overflow rejects the chunk", !client.receive(args));
    });
    worker.join();
    check("teardown completion reconnect gets no stale overflow disconnect", reconnected && staleUnclean == 0 && unclean == 0);
    check("completion reconnect leaves replacement connected", client.isConnected());
    internal::drainMainThreadQueue();
}

void overflowReconnect() {
    ReentrantDeliveryClient client;
    int inlineCalls = 0, mainCalls = 0, clean = 0, unclean = 0;
    bool reconnected = false;
    auto main = client.onReceive.listen([&](auto&) { ++mainCalls; }, Deliver::Main);
    auto inlineListener = client.onReceive.listen([&](auto&) {
        if (++inlineCalls == 1025) reconnected = client.connect("replacement", 2);
    });
    auto close = client.onDisconnect.listen([&](auto& args) {
        if (args.wasClean) ++clean;
        else ++unclean;
    });
    check("initial transport connected", client.connect("initial", 1));
    std::thread worker([&] {
        TcpReceiveEventArgs args; args.data = {'a'};
        for (int i = 0; i < 1024; ++i) check("reentrant test fills receive credits", client.receive(args));
        check("reentrant overflow rejects the chunk", !client.receive(args));
    });
    worker.join();
    check("Inline overflow reconnect emits exactly one clean disconnect", clean == 1 && unclean == 0);
    check("overflow leaves replacement connected", reconnected && client.isConnected());
    internal::drainMainThreadQueue();
    check("retired overflow run still invalidates queued chunks", mainCalls == 0);
    TcpReceiveEventArgs replacement; replacement.data = {'b'};
    std::thread replacementWorker([&] { check("replacement admits fresh receive", client.receive(replacement)); });
    replacementWorker.join();
    internal::drainMainThreadQueue();
    check("replacement receives after retired overflow", mainCalls == 1 && client.isConnected());
}

void tcpOverflow(bool serverSide) {
    TcpServer server;
    server.setBindAddress("127.0.0.1");
    server.setReceiveBufferSize(1);
    check("TCP server starts", server.start(0));
    TcpClient client;
    client.setReceiveBufferSize(1);
    std::atomic<int> received{0}, disconnected{0};
    int mainCalls = 0, mainDisconnects = 0, completions = 0;
    std::atomic<bool> completionQueued{false};
    EventListener data, observe, close, closeMain, completion, completionObserved;
    if (serverSide) {
        data = server.onReceive.listen([&](TcpServerReceiveEventArgs&) { ++mainCalls; }, Deliver::Main);
        observe = server.onReceive.listen([&](TcpServerReceiveEventArgs&) { ++received; });
        closeMain = server.onClientDisconnect.listen([&](TcpClientDisconnectEventArgs&) { ++mainDisconnects; }, Deliver::Main);
        close = server.onClientDisconnect.listen([&](TcpClientDisconnectEventArgs& p) { if (!p.wasClean) ++disconnected; });
    } else {
        data = client.onReceive.listen([&](TcpReceiveEventArgs&) { ++mainCalls; }, Deliver::Main);
        observe = client.onReceive.listen([&](TcpReceiveEventArgs&) { ++received; });
        closeMain = client.onDisconnect.listen([&](TcpDisconnectEventArgs&) { ++mainDisconnects; }, Deliver::Main);
        close = client.onDisconnect.listen([&](TcpDisconnectEventArgs& p) { if (!p.wasClean) ++disconnected; });
        completion = server.onSendComplete.listen([&](TcpSendCompleteEventArgs&) { ++completions; }, Deliver::Main);
        completionObserved = server.onSendComplete.listen([&](TcpSendCompleteEventArgs&) { completionQueued = true; });
    }
    check("TCP loopback connects", client.connect("127.0.0.1", server.getPort()));
    check("TCP accepted", until([&] { return server.getClientCount() == 1; }));
    std::string bytes(1025, 't');
    if (serverSide) check("send to server", client.send(bytes));
    else server.broadcast(bytes);
    check("TCP overflow disconnect reported without drain", until([&] { return disconnected == 1; }));
    check("TCP Inline observes the chunk whose Main copy overflowed", received == 1025);
    check("no Main receive runs during stall", mainCalls == 0);
    check("overflow closes TCP run", serverSide ? until([&] { return server.getClientCount() == 0; }) : !client.isConnected());
    // Observers are registered after the Main listeners, so these signals
    // establish queue admission, independently of worker scheduling.
    if (!serverSide) check("send completion queued", until([&] { return completionQueued.load(); }));
    internal::drainMainThreadQueue();
    check("all queued chunks of overflowed TCP run invalidated", mainCalls == 0);
    check("disconnect teardown delivery preserved", mainDisconnects == 1);
    if (!serverSide) check("send completion delivery preserved", completions == 1);
    client.disconnect();
    check("old TCP client removed", until([&] { return server.getClientCount() == 0; }));
    check("TCP reconnect after overflow", client.connect("127.0.0.1", server.getPort()));
    check("replacement TCP accepted", until([&] { return server.getClientCount() == 1; }));
    if (serverSide) check("send replacement byte", client.send("r"));
    else server.broadcast("r");
    check("replacement run receives", until([&] { return received == 1026; }));
    internal::drainMainThreadQueue();
    check("replacement TCP run has a fresh delivery token", mainCalls == 1);
    client.disconnect();
    server.stop();
    internal::drainMainThreadQueue();
}

void retainedSnapshotDestruction() {
    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto releaseFuture = release.get_future().share();
    auto source = std::make_unique<Event<int>>();
    int calls = 0;
    auto mainListener = source->listen([&](int&) { ++calls; }, Deliver::Main);
    auto inlineListener = source->listen([&](int&) { entered.set_value(); releaseFuture.wait(); });
    auto* event = source.get();
    std::thread worker([&] { int value = 0; External<int>::notify(*event, value); });
    check("external dispatch retains snapshot on worker", enteredFuture.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    auto store = External<int>::diagnostics(*source);
    source.reset();
    internal::drainMainThreadQueue();
    check("Event destruction cancels queued Main with snapshot still retained", calls == 0);
    release.set_value();
    worker.join();
    check("retained-snapshot destruction releases all credits", store->pending == 0);

    std::promise<void> voidEntered, voidRelease;
    auto voidReady = voidEntered.get_future();
    auto voidProceed = voidRelease.get_future().share();
    auto voidSource = std::make_unique<Event<void>>();
    auto voidMain = voidSource->listen([&] { ++calls; }, Deliver::Main);
    auto voidInline = voidSource->listen([&] { voidEntered.set_value(); voidProceed.wait(); });
    auto* voidEvent = voidSource.get();
    std::thread voidWorker([&] { External<void>::notify(*voidEvent); });
    check("void dispatch retains snapshot on worker", voidReady.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    auto voidStore = External<void>::diagnostics(*voidSource);
    voidSource.reset();
    internal::drainMainThreadQueue();
    check("void Event destruction cancels queued Main with snapshot retained", calls == 0);
    voidRelease.set_value();
    voidWorker.join();
    check("void retained-snapshot destruction releases credits", voidStore->pending == 0);

    int synchronousCalls = 0;
    auto synchronous = std::make_unique<Event<int>>();
    auto destroy = synchronous->listen([&](int&) { synchronous.reset(); });
    auto finish = synchronous->listen([&](int&) { ++synchronousCalls; }, Deliver::Main);
    int value = 0;
    synchronous->notify(value);
    check("already-main delivery finishes synchronous snapshot after source death", synchronousCalls == 1);
    auto synchronousVoid = std::make_unique<Event<void>>();
    auto destroyVoid = synchronousVoid->listen([&] { synchronousVoid.reset(); });
    auto finishVoid = synchronousVoid->listen([&] { ++synchronousCalls; }, Deliver::Main);
    synchronousVoid->notify();
    check("already-main void delivery preserves synchronous snapshot", synchronousCalls == 2);
}

void callableCopies() {
    for (bool external : {false, true}) {
        Event<int> typed;
        Event<void> empty;
        std::vector<int> typedSeen, voidSeen;
        auto typedListener = typed.listen([n = 0, &typedSeen](int&) mutable { typedSeen.push_back(++n); }, Deliver::Main);
        auto voidListener = empty.listen([n = 0, &voidSeen]() mutable { voidSeen.push_back(++n); }, Deliver::Main);
        std::thread worker([&] {
            int value = 0;
            for (int i = 0; i < 2; ++i) {
                if (external) { External<int>::notify(typed, value); External<void>::notify(empty); }
                else { typed.notify(value); empty.notify(); }
            }
        });
        worker.join();
        internal::drainMainThreadQueue();
        int value = 0;
        typed.notify(value);
        empty.notify();
        check("typed queued callables retain independent mutable captures", typedSeen == std::vector<int>({1, 1, 1}));
        check("void queued callables retain independent mutable captures", voidSeen == std::vector<int>({1, 1, 1}));
    }
}

void orderedCopies() {
    Event<std::string> edited;
    std::vector<std::string> seen;
    auto inlineEdit = edited.listen([](std::string& value) { value = "edited"; });
    auto first = edited.listen([&](std::string& value) { seen.push_back(value); }, Deliver::Main);
    auto inlineEditAgain = edited.listen([](std::string& value) { value += " again"; });
    auto second = edited.listen([&](std::string& value) { seen.push_back(value); }, Deliver::Main);
    std::thread producer([&] { std::string value = "original"; External<std::string>::notify(edited, value); });
    producer.join();
    internal::drainMainThreadQueue();
    std::string value = "original";
    External<std::string>::notify(edited, value);
    check("Main copies see earlier Inline edits on worker and main", seen == std::vector<std::string>({"edited", "edited again", "edited", "edited again"}));

    Event<std::string> growing;
    int calls = 0, inlineCalls = 0;
    auto small = growing.listen([&](std::string& value) { check("earlier copy keeps its complete value", value == "small"); ++calls; }, Deliver::Main);
    auto grow = growing.listen([](std::string& value) { value.assign(Store::byteLimit + 1, 'x'); });
    auto large = growing.listen([&](std::string&) { ++calls; }, Deliver::Main);
    auto tail = growing.listen([&](std::string&) { ++inlineCalls; });
    size_t charge = 0;
    std::thread grower([&] {
        std::string input = "small";
        charge = internal::ExternalPayload<std::string>::bytes(input);
        check("later oversized copy rejected after Inline growth", !External<std::string>::notify(growing, input));
    });
    grower.join();
    auto store = External<std::string>::diagnostics(growing);
    check("only storage measured at each listener turn is reserved", store->pending == 1 && store->retainedBytes == charge && store->rejectedCopies == 1);
    check("rejected Main copy preserves subsequent Inline delivery", inlineCalls == 1);
    internal::drainMainThreadQueue();
    check("Inline growth cannot escape admission", calls == 1 && store->pending == 0 && store->retainedBytes == 0);
}

struct DeliveryClient : TcpClient {
    void connectedForTest() { state()->connected_ = true; }
    bool receiveForTest(TcpReceiveEventArgs& args) { return state()->deliverReceive(args); }
};

void appNetworkNotifications() {
    DeliveryClient client;
    int errors = 0, clean = 0, overflow = 0, receives = 0, inlineCloses = 0;
    auto error = client.onError.listen([&](TcpErrorEventArgs&) { ++errors; }, Deliver::Main);
    auto close = client.onDisconnect.listen([&](TcpDisconnectEventArgs& args) {
        if (args.wasClean) ++clean;
        if (args.reason == "External main-thread delivery overflow") ++overflow;
    }, Deliver::Main);
    auto inlineClose = client.onDisconnect.listen([&](TcpDisconnectEventArgs&) { ++inlineCloses; });
    auto receive = client.onReceive.listen([&](TcpReceiveEventArgs&) { ++receives; }, Deliver::Main);
    std::thread worker([&] {
        // Fill the external disconnect store independently of application
        // operations, so teardown must bypass an already saturated store.
        TcpDisconnectEventArgs peerClose;
        peerClose.wasClean = false;
        for (int i = 0; i < 1024; ++i) External<TcpDisconnectEventArgs>::notify(client.onDisconnect, peerClose);
        for (int i = 0; i < 1025; ++i) {
            client.sendAsync("not connected");
            client.connectedForTest();
            client.disconnect();
        }
        TcpReceiveEventArgs chunk;
        chunk.data = {'x'};
        for (int i = 0; i < 1024; ++i) check("receive copy admitted before overflow", client.receiveForTest(chunk));
        check("receive overflow closes and reports", !client.receiveForTest(chunk));
    });
    worker.join();
    auto closes = External<TcpDisconnectEventArgs>::diagnostics(client.onDisconnect);
    auto data = External<TcpReceiveEventArgs>::diagnostics(client.onReceive);
    check("app disconnects and overflow bypass saturated disconnect store", closes->pending == 1024 && closes->rejectedCopies == 0 && inlineCloses == 2050);
    check("app send rejections never allocate an external store", !External<TcpErrorEventArgs>::diagnostics(client.onError));
    check("overflow leaves receive store saturated until queued copies die", data->pending == 1024 && data->rejectedCopies == 1);
    internal::drainMainThreadQueue();
    check("all 1025 sendAsync errors delivered", errors == 1025);
    check("all 1025 application disconnects delivered", clean == 1025);
    check("overflow teardown delivered with both external stores full", overflow == 1 && receives == 0);
}

struct MoveOnlyProbe {
    MoveOnlyProbe() = default;
    MoveOnlyProbe(const MoveOnlyProbe&) = delete;
};

void lifetimeAndAppWork() {
    int calls = 0;
    auto source = std::make_unique<Event<int>>();
    auto listener = source->listen([&](int&) { ++calls; }, Deliver::Main);
    std::thread worker([&] { int payload = 0; External<int>::notify(*source, payload); });
    worker.join();
    auto store = External<int>::diagnostics(*source);
    source.reset();
    internal::drainMainThreadQueue();
    check("source destruction cancels external delivery and releases credits", calls == 0 && store->pending == 0);

    Event<int> cleared;
    auto removed = cleared.listen([&](int&) { ++calls; }, Deliver::Main);
    std::thread queued([&] { int value = 0; External<int>::notify(cleared, value); });
    queued.join();
    auto clearedStore = External<int>::diagnostics(cleared);
    cleared.clear();
    internal::drainMainThreadQueue();
    check("clear cancels external deliveries and releases credits", calls == 0 && clearedStore->pending == 0);

    Event<int> inlineSource;
    auto inlineListener = inlineSource.listen([](int&) {});
    std::thread inlineProducer([&] { int value = 0; External<int>::notify(inlineSource, value); });
    inlineProducer.join();
    check("external store stays unallocated without Main listeners", !External<int>::diagnostics(inlineSource));

    Event<MoveOnlyProbe> moveOnly;
    int inlineCalls = 0;
    bool workerDelivery = false;
    auto moveListener = moveOnly.listen([&](MoveOnlyProbe&) { ++inlineCalls; workerDelivery = !isMainThread(); }, Deliver::Main);
    std::thread moveProducer([&] { MoveOnlyProbe value; moveOnly.notify(value); });
    moveProducer.join();
    check("noncopyable Main payload preserves inline fallback", inlineCalls == 1 && workerDelivery);

    Event<int> app;
    auto appListener = app.listen([&](int&) { ++calls; }, Deliver::Main);
    int jobs = 0;
    std::thread producer([&] {
        int value = 0;
        for (int i = 0; i < 10000; ++i) {
            app.notify(value);
            runOnMainThread([&] { ++jobs; });
        }
        runOnMainThread([&] { jobs += 7; }); // teardown/completion work
    });
    producer.join();
    check("app event never allocates external store", !External<int>::diagnostics(app));
    internal::drainMainThreadQueue();
    check("app events and jobs remain growable", calls == 10000 && jobs == 10007);
}
}

TC_CORE_TEST_MAIN() {
    Watchdog watchdog;
    getMainThreadId();
    std::atomic<int> warnings{0};
    std::atomic<bool> safeWarnings{true};
    auto logger = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.message.find("[ExternalDelivery]") != std::string::npos && e.level == LogLevel::Warning) {
            ++warnings;
            if (!isMainThread()) safeWarnings = false;
        }
    });
    if (const char* selected = std::getenv("TC_EXTERNAL_DELIVERY_CASE")) {
        const std::string name(selected);
        if (name == "lock-order") lockOrder();
        else if (name == "osc-errors") oscForwardedErrors();
        else if (name == "overflow-reconnect") overflowReconnect();
        else if (name == "overflow-completion") overflowCompletionReconnect();
        else return 2;
        return failures ? 1 : 0;
    }
    lockOrder();
    countAndFanout();
    check("first loss Warning delivered automatically", warnings == 1 && safeWarnings);
    bytesAndUnits();
    oscAndMidi();
    callableCopies();
    orderedCopies();
    appNetworkNotifications();
    oscForwardedErrors();
    overflowReconnect();
    overflowCompletionReconnect();
    tcpOverflow(false);
    tcpOverflow(true);
    retainedSnapshotDestruction();
    lifetimeAndAppWork();
    check("one Warning per rejecting store on main", warnings == 12 && safeWarnings);
    return failures ? 1 : 0;
}
