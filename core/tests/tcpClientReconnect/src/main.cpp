// =============================================================================
// core/tests/tcpClientReconnect — regression test for TcpClient::connect()
// after the peer closed the connection (#254).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariants:
//   - connect() on a client whose peer closed the connection reconnects. The
//     remote close only cleared the flags, so connect() skipped its cleanup,
//     overwrote the still-open socket and assigned the new receive thread over
//     the old, still-joinable one: std::terminate.
//   - Reconnecting over and over leaks no descriptors (checked on Linux).
//   - connect() from a plain (inline) onDisconnect listener, which runs on the
//     receive thread, reconnects too, and afterwards exactly one receive
//     thread is left (checked on Linux): the old thread has to stop even
//     though running_ is true again for the new connection.
//   - Reconnecting through connectAsync() works too. connect() then runs on
//     connectThread_, which its cleanup must leave alone, and each call joins
//     the previous call's finished connect thread.
//   - A reconnect the peer refuses (its device still rebooting) returns false
//     and still releases the old socket (checked on Linux), and the next
//     connect() succeeds.
//   - With an auto-reconnect onDisconnect listener attached (reconnect unless
//     the reason is "Disconnected by client"), disconnect() from another
//     thread reports exactly one onDisconnect, "Disconnected by client", and
//     leaves no connection. Destroying such a client neither hangs nor
//     reconnects. The receive thread used to report the EOF of disconnect()'s
//     own shutdown() as a remote close, and the listener reconnected from it
//     while disconnect() was joining that thread.
//
// The pre-fix build aborts on the first reconnect. The scenario runs on a
// worker with a deadline, so a hang reports FAIL instead of eating the CI
// job timeout.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define TC_CLOSE closesocket
    using rawsocket_t = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <poll.h>
    #include <sys/time.h>
    #define TC_CLOSE ::close
    using rawsocket_t = int;
#endif

#ifdef __linux__
    #include <dirent.h>
#endif

using namespace std;
using namespace tc;

static const rawsocket_t kNoSocket = static_cast<rawsocket_t>(-1);

static atomic<int> g_fail{0};
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);   // flush per line so CI logs survive a later abort
    if (!ok) ++g_fail;
}

// Run fn on a worker and report failure if it does not finish in time. The
// thread is detached so a hang still ends in a FAIL line and a non-zero exit.
template <typename F>
static bool completesWithin(int ms, F fn) {
    auto done = make_shared<atomic<bool>>(false);
    thread([done, fn = move(fn)]() mutable { fn(); done->store(true); }).detach();
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (chrono::steady_clock::now() < deadline) {
        if (done->load()) return true;
        this_thread::sleep_for(chrono::milliseconds(5));
    }
    return done->load();
}

// Poll pred until it holds or ms pass
template <typename P>
static bool waitFor(int ms, P pred) {
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        this_thread::sleep_for(chrono::milliseconds(2));
    }
    return pred();
}

// Leave without running destructors: a client left in a broken state may
// hang in its destructor, and the result is already decided.
[[noreturn]] static void bail() {
    printf("\nFAILED\n");
    fflush(stdout);
    _Exit(1);
}

// A TCP socket bound to 127.0.0.1 with a port the OS picks
static rawsocket_t bindLoopback(int& port) {
    rawsocket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNoSocket) return kNoSocket;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    socklen_t len = sizeof(addr);
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        ::getsockname(s, (sockaddr*)&addr, &len) != 0) {
        TC_CLOSE(s);
        return kNoSocket;
    }
    port = ntohs(addr.sin_port);
    return s;
}

// A listening socket on 127.0.0.1 with a port the OS picks
static rawsocket_t listenLoopback(int& port) {
    rawsocket_t s = bindLoopback(port);
    if (s == kNoSocket) return kNoSocket;
    if (::listen(s, 8) != 0) {
        TC_CLOSE(s);
        return kNoSocket;
    }
    return s;
}

static rawsocket_t acceptWithin(rawsocket_t listener, int ms) {
#ifdef _WIN32
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(listener, &fds);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    if (::select(0, &fds, nullptr, nullptr, &tv) <= 0) return kNoSocket;
#else
    pollfd pfd{listener, POLLIN, 0};
    if (::poll(&pfd, 1, ms) <= 0) return kNoSocket;
#endif
    return ::accept(listener, nullptr, nullptr);
}

static void setRecvTimeout(rawsocket_t s, int ms) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

// The client sends msg; the raw peer must read exactly that
static bool clientToPeer(TcpClient& client, rawsocket_t peer, const string& msg) {
    if (!client.send(msg)) return false;
    setRecvTimeout(peer, 2000);
    string got;
    char buf[256];
    while (got.size() < msg.size()) {
        int n = static_cast<int>(::recv(peer, buf, sizeof(buf), 0));
        if (n <= 0) break;
        got.append(buf, n);
    }
    return got == msg;
}

#ifdef __linux__
// Entries in a /proc directory: open descriptors or threads of this process
static int countEntries(const char* path) {
    DIR* d = opendir(path);
    if (!d) return -1;
    int n = 0;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] != '.') ++n;
    }
    closedir(d);
    return n;
}
#endif

static void scenario() {
    TcpClient client;   // constructed first: it also starts Winsock

    int port = 0;
    rawsocket_t listener = listenLoopback(port);
    check("loopback listener is up", listener != kNoSocket);
    if (listener == kNoSocket) bail();

    // Everything the client receives, from its receive thread
    mutex rxMutex;
    string received;
    EventListener rxSub = client.onReceive.listen([&](TcpReceiveEventArgs& e) {
        lock_guard<mutex> lock(rxMutex);
        received.append(e.data.begin(), e.data.end());
    });

    // The raw peer sends msg; the client must receive exactly that
    auto peerToClient = [&](rawsocket_t p, const string& msg) {
        {
            lock_guard<mutex> lock(rxMutex);
            received.clear();
        }
        ::send(p, msg.data(), static_cast<int>(msg.size()), 0);
        return waitFor(3000, [&] {
            lock_guard<mutex> lock(rxMutex);
            return received == msg;
        });
    };

    // --- first connection ---------------------------------------------------
    check("initial connect()", client.connect("127.0.0.1", port));
    rawsocket_t peer = acceptWithin(listener, 2000);
    check("peer accepted the connection", peer != kNoSocket);
    if (peer == kNoSocket) bail();
    check("data reaches the peer", clientToPeer(client, peer, "hello"));

    // --- reconnect after the peer closed, repeatedly --------------------------
    const int rounds = 20;
    bool noticed = true, reconnected = true, accepted = true, delivered = true;
#ifdef __linux__
    int fdsAfterFirst = -1, fdsAfterLast = -1;
#endif
    for (int i = 0; i < rounds; ++i) {
        TC_CLOSE(peer);   // the client's receive thread sees EOF
        if (!waitFor(2000, [&] { return !client.isConnected(); })) { noticed = false; break; }

        // Pre-fix: std::terminate right here
        if (!client.connect("127.0.0.1", port)) { reconnected = false; break; }
        peer = acceptWithin(listener, 2000);
        if (peer == kNoSocket) { accepted = false; break; }
        if (!clientToPeer(client, peer, "ping " + to_string(i))) { delivered = false; break; }

#ifdef __linux__
        const int fds = countEntries("/proc/self/fd");
        if (i == 0) fdsAfterFirst = fds;
        fdsAfterLast = fds;
#endif
    }
    check("client notices each remote close", noticed);
    check("connect() after a remote close succeeds (20 rounds)", reconnected);
    check("peer accepts every reconnect", accepted);
    check("data reaches the peer after every reconnect", delivered);
    if (g_fail) bail();

#ifdef __linux__
    printf("  (open descriptors: %d after the first reconnect, %d after the last)\n",
           fdsAfterFirst, fdsAfterLast);
    check("reconnecting leaks no descriptors", fdsAfterLast <= fdsAfterFirst);
#else
    printf("%-60s %s\n", "reconnecting leaks no descriptors", "SKIP (counted on Linux)");
#endif

    // --- reconnect from an inline onDisconnect listener -----------------------
    // The listener runs on the receive thread, inside processNetwork(). Pre-fix
    // that also ended in std::terminate; with the old thread merely detached,
    // it kept running next to the new one, both reading the same socket.
#ifdef __linux__
    const int threadsBefore = countEntries("/proc/self/task");
#endif
    atomic<bool> armed{true};
    atomic<int> listenerResult{-1};   // -1 not run, 0 connect() failed, 1 ok
    EventListener reconnectSub = client.onDisconnect.listen([&](TcpDisconnectEventArgs&) {
        if (armed.exchange(false)) {
            listenerResult = client.connect("127.0.0.1", port) ? 1 : 0;
        }
    });

    TC_CLOSE(peer);
    peer = acceptWithin(listener, 3000);
    check("listener reconnect: peer accepted the new connection", peer != kNoSocket);
    check("listener reconnect: connect() returned true",
          waitFor(3000, [&] { return listenerResult.load() == 1; }));
    check("listener reconnect: client is connected",
          waitFor(3000, [&] { return client.isConnected(); }));
    if (g_fail) bail();
    reconnectSub.disconnect();

    check("listener reconnect: data reaches the peer", clientToPeer(client, peer, "after"));
    check("listener reconnect: the client receives the peer's data",
          peerToClient(peer, "pong from the peer"));

#ifdef __linux__
    // The detached thread finishes within a millisecond or so once it sees it
    // is no longer current; give it ample time.
    int threadsAfter = -1;
    waitFor(1000, [&] {
        threadsAfter = countEntries("/proc/self/task");
        return threadsAfter <= threadsBefore;
    });
    printf("  (threads: %d before, %d after)\n", threadsBefore, threadsAfter);
    check("listener reconnect: the old receive thread stopped", threadsAfter <= threadsBefore);
#else
    printf("%-60s %s\n", "listener reconnect: the old receive thread stopped",
           "SKIP (counted on Linux)");
#endif
    if (g_fail) bail();

    // --- reconnect through connectAsync() -----------------------------------
    // connect() runs on connectThread_ here, and its cleanup must not join or
    // detach that thread. From the second round on, connectAsync() also joins
    // the previous round's finished connect thread.
    atomic<int> asyncConnects{0};
    EventListener connectSub = client.onConnect.listen([&](TcpConnectEventArgs& e) {
        if (e.success) ++asyncConnects;
    });
    bool asyncNoticed = true, asyncConnected = true, asyncAccepted = true, asyncDelivered = true;
    for (int i = 0; i < 3; ++i) {
        TC_CLOSE(peer);
        if (!waitFor(2000, [&] { return !client.isConnected(); })) { asyncNoticed = false; break; }
        const int before = asyncConnects.load();
        client.connectAsync("127.0.0.1", port);
        if (!waitFor(3000, [&] { return asyncConnects.load() > before; })) { asyncConnected = false; break; }
        peer = acceptWithin(listener, 2000);
        if (peer == kNoSocket) { asyncAccepted = false; break; }
        if (!clientToPeer(client, peer, "async " + to_string(i))) { asyncDelivered = false; break; }
    }
    connectSub.disconnect();
    check("connectAsync(): client notices each remote close", asyncNoticed);
    check("connectAsync() after a remote close connects (3 rounds)", asyncConnected);
    check("connectAsync(): peer accepts every reconnect", asyncAccepted);
    check("connectAsync(): data reaches the peer after every reconnect", asyncDelivered);
    if (g_fail) bail();
    check("connectAsync(): the client receives the peer's data",
          peerToClient(peer, "pong after connectAsync"));
    if (g_fail) bail();

    // --- refused reconnects, then a successful one --------------------------
    // The usual recovery while the peer device is still rebooting. The refused
    // port is bound but not listening, and held until the end, so nothing else
    // can take it and the client's own ephemeral port cannot be it (on Linux a
    // connect() to a free ephemeral port can connect to itself).
    int refusedPort = 0;
    rawsocket_t refusedSock = bindLoopback(refusedPort);
    check("refused port is reserved", refusedSock != kNoSocket);
    if (refusedSock == kNoSocket) bail();

    TC_CLOSE(peer);
    peer = kNoSocket;
    check("refused reconnect: client notices the remote close",
          waitFor(2000, [&] { return !client.isConnected(); }));
    if (g_fail) bail();
#ifdef __linux__
    // Includes the client's old socket, which the peer closed
    const int fdsBeforeRefused = countEntries("/proc/self/fd");
#endif
    bool refused = true;
    for (int i = 0; i < 3; ++i) {
        if (client.connect("127.0.0.1", refusedPort)) refused = false;
    }
    check("refused reconnect: connect() returns false (3 attempts)", refused);
    check("refused reconnect: client is not connected", !client.isConnected());
    if (g_fail) bail();
#ifdef __linux__
    const int fdsAfterRefused = countEntries("/proc/self/fd");
    printf("  (open descriptors: %d before the refused attempts, %d after)\n",
           fdsBeforeRefused, fdsAfterRefused);
    check("refused reconnect: the old socket is released", fdsAfterRefused < fdsBeforeRefused);
#else
    printf("%-60s %s\n", "refused reconnect: the old socket is released",
           "SKIP (counted on Linux)");
#endif

    check("refused reconnect: a later connect() succeeds", client.connect("127.0.0.1", port));
    peer = acceptWithin(listener, 2000);
    check("refused reconnect: peer accepts the later connect()", peer != kNoSocket);
    if (g_fail) bail();
    check("refused reconnect: data reaches the peer afterwards", clientToPeer(client, peer, "back"));
    check("refused reconnect: the client receives the peer's data",
          peerToClient(peer, "welcome back"));
    if (g_fail) bail();

    // --- disconnect() with an auto-reconnect listener attached ---------------
    // The usual auto-reconnect: an inline onDisconnect listener that reconnects
    // unless the app itself disconnected. disconnect()'s shutdown() wakes the
    // receive thread with EOF. When that thread reported it as a remote close,
    // the listener reconnected from it while disconnect() was still joining it
    // (the thread detached itself under the join), and disconnect() then saw
    // the new connection and reported a second disconnect.
    mutex reasonsMutex;
    vector<string> reasons;
    atomic<int> autoReconnects{0};
    auto autoReconnect = [&](TcpClient* c) {
        return c->onDisconnect.listen([&, c](TcpDisconnectEventArgs& e) {
            {
                lock_guard<mutex> lock(reasonsMutex);
                reasons.push_back(e.reason);
            }
            if (e.reason != "Disconnected by client") {
                ++autoReconnects;
                c->connect("127.0.0.1", port);
            }
        });
    };
    // A connection the client should not have made. Waiting for it is also
    // the grace period for a late onDisconnect from a stray receive thread.
    auto strayConnection = [&] {
        rawsocket_t stray = acceptWithin(listener, 300);
        if (stray == kNoSocket) return false;
        TC_CLOSE(stray);
        return true;
    };

    EventListener autoSub = autoReconnect(&client);
    thread([&] { client.disconnect(); }).join();   // not the receive thread
    const bool strayAfterDisconnect = strayConnection();
    {
        lock_guard<mutex> lock(reasonsMutex);
        check("auto-reconnect: disconnect() reports one onDisconnect",
              reasons.size() == 1);
        check("auto-reconnect: it says \"Disconnected by client\"",
              reasons.size() == 1 && reasons[0] == "Disconnected by client");
    }
    check("auto-reconnect: disconnect() does not reconnect",
          autoReconnects == 0 && !strayAfterDisconnect);
    check("auto-reconnect: the client is left disconnected", !client.isConnected());
    autoSub.disconnect();
    TC_CLOSE(peer);
    if (g_fail) bail();

    // --- destroying a client with the auto-reconnect listener attached ------
    // The destructor calls disconnect(): the same EOF, on a client about to go.
    auto doomed = make_unique<TcpClient>();
    check("destroyed client: connect()", doomed->connect("127.0.0.1", port));
    peer = acceptWithin(listener, 2000);
    check("destroyed client: peer accepted the connection", peer != kNoSocket);
    if (g_fail) bail();
    // Data from the peer puts the receive thread in its receive loop, blocked
    // in recv() by the time the destructor runs, as it is in a live app
    atomic<bool> doomedReceived{false};
    EventListener doomedRx = doomed->onReceive.listen([&](TcpReceiveEventArgs&) {
        doomedReceived = true;
    });
    ::send(peer, "hi", 2, 0);
    check("destroyed client: the client receives the peer's data",
          waitFor(3000, [&] { return doomedReceived.load(); }));
    if (g_fail) bail();
    {
        lock_guard<mutex> lock(reasonsMutex);
        reasons.clear();
    }
    autoReconnects = 0;
    EventListener doomedSub = autoReconnect(doomed.get());
    check("destroyed client: the destructor finishes within 5 s",
          completesWithin(5000, [&] { doomed.reset(); }));
    if (g_fail) bail();
    const bool strayAfterDestroy = strayConnection();
    check("destroyed client: no reconnect after destruction",
          autoReconnects == 0 && !strayAfterDestroy);
    {
        lock_guard<mutex> lock(reasonsMutex);
        bool onlyByClient = true;
        for (const string& r : reasons) {
            if (r != "Disconnected by client") onlyByClient = false;
        }
        check("destroyed client: no disconnect reported as a remote close", onlyByClient);
    }
    // doomedSub outlives its Event: disconnecting it now is a no-op

    // --- teardown ---------------------------------------------------------
    TC_CLOSE(peer);
    TC_CLOSE(refusedSock);
    TC_CLOSE(listener);
}

int main() {
    if (!completesWithin(60000, scenario)) {
        check("scenario finished within 60 s", false);
        bail();
    }
    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
