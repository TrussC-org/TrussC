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
//   - connect() from an inline onReceive listener reconnects too, and the old
//     receive thread stops (checked on Linux) instead of going back to recv()
//     on the new socket: the generation check covers processNetwork()'s receive
//     loop as well, not only the loop around it.
//   - Reconnecting through connectAsync() works too. connect() then runs on
//     connectThread_, which its cleanup must leave alone, and each call joins
//     the previous call's finished connect thread.
//   - A reconnect the peer refuses (its device still rebooting) returns false
//     and still releases the old socket (checked on Linux), and the next
//     connect() succeeds.
//   - An onError listener that reconnects after a refused connect() keeps
//     its connection: connect() closes the failed socket before notifying.
//     The same without threads, where the refused connect is pending and
//     processNetwork() (driven by the update event) reports the failure.
//   - The "bye" pattern: an onReceive listener calls disconnect() and the
//     main thread reconnects. The old receive thread reads none of the new
//     connection's data and stops (counted on Linux); without threads it
//     does not drive the new connection either (onConnect fires on the main
//     thread). connect() used to set the flags before it bumped the
//     generation, and a reconnect without threads did not bump it at all.
//   - connect() to another peer while connected, with a listener that
//     reconnects on every onDisconnect: the listener's reconnect (to the old
//     peer, from inside connect()'s own disconnect) is closed again without
//     another notification, connect() reaches the new peer, and nothing leaks
//     (counted on Linux). Its receive thread used to report the shutdown's
//     EOF as a remote close, and the listener reconnected again from that
//     thread while connect() was joining it.
//   - With an auto-reconnect onDisconnect listener attached (reconnect unless
//     the reason is "Disconnected by client"), disconnect() from another
//     thread reports exactly one onDisconnect, "Disconnected by client", and
//     leaves no connection. The receive thread used to report the EOF of
//     disconnect()'s own shutdown() as a remote close, and the listener
//     reconnected from it while disconnect() was joining that thread.
//   - The destructor fires no onDisconnect: destroying a client whose
//     listener reconnects on every onDisconnect finishes, reports nothing and
//     does not reconnect.
//
//   - No thread is left when main() returns (counted on Linux), although the
//     clients whose listeners called connect() / disconnect() on the receive
//     thread are kept for the life of the process.
//
// The pre-fix build aborts on the first reconnect. The scenario runs on a
// worker with a deadline, so a hang reports FAIL instead of eating the CI
// job timeout. A crash prints what the test was doing and a backtrace.
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

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__ANDROID__)
    #define TC_TEST_CRASH_REPORT 1
    #include <csignal>
    #include <execinfo.h>
#endif

using namespace std;
using namespace tc;

static const rawsocket_t kNoSocket = static_cast<rawsocket_t>(-1);

static atomic<int> g_fail{0};

// Set on the thread that ran a listener's "bye" disconnect(). A thread id
// cannot tell that thread apart: once it exits, the next thread may get the
// same id.
static thread_local bool t_byeThread = false;
// What the test is doing, for the fatal-signal report below
static const char* volatile g_phase = "starting";

#ifdef TC_TEST_CRASH_REPORT
// A crash prints what the test was doing and a backtrace, then dies of the
// same signal. macOS CI once died of SIGSEGV here after the last check, which
// no run on Linux reproduces; this says where the next one happens.
static void onFatalSignal(int sig) {
    char line[160];
    const int n = snprintf(line, sizeof(line), "\nFATAL: signal %d during %s\n",
                           sig, g_phase);
    if (n > 0) (void)!write(2, line, static_cast<size_t>(n));
    void* frames[64];
    backtrace_symbols_fd(frames, backtrace(frames, 64), 2);
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);   // flush per line so CI logs survive a later abort
    if (!ok) ++g_fail;
}

// Run fn on a worker and report failure if it does not finish in time. A
// worker that finished is joined, so it is not still exiting when main()
// returns and the process tears down its statics; one that hangs is
// detached, and every caller then bails out with _Exit.
template <typename F>
static bool completesWithin(int ms, F fn) {
    auto done = make_shared<atomic<bool>>(false);
    thread worker([done, fn = move(fn)]() mutable { fn(); done->store(true); });
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (!done->load() && chrono::steady_clock::now() < deadline) {
        this_thread::sleep_for(chrono::milliseconds(5));
    }
    if (done->load()) {
        worker.join();
        return true;
    }
    worker.detach();
    return false;
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

// A port on 127.0.0.1 that refuses a connect(): one nothing is bound to,
// below every platform's ephemeral range (Linux 32768+, macOS and Windows
// 49152+), so a client's own ephemeral port cannot be it (on Linux a connect()
// to a free ephemeral port can connect to itself). Linux refuses at once;
// Windows after its SYN retries (about 2 s on loopback; measured 2.02-2.06 s).
// Not a socket bound without listen(): with that, macOS drops the SYN (its TCP
// drops segments for a socket in the CLOSED state), so the connect() fails
// only once it gives up, about 8 s later on the CI runner.
// Returns 0 if no port in the range is free.
static int refusingPort() {
    const int base = 21000, span = 8000;
    const int start = static_cast<int>(
        chrono::steady_clock::now().time_since_epoch().count() % span);
    for (int i = 0; i < span; ++i) {
        const int port = base + (start + i) % span;
        rawsocket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == kNoSocket) return 0;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        const bool free = ::bind(s, (sockaddr*)&addr, sizeof(addr)) == 0;
        TC_CLOSE(s);
        if (free) return port;
    }
    return 0;
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
    // Constructed first: it also starts Winsock. Kept for the life of the
    // process, as TcpClient's Events comment asks of a client whose
    // receive-thread listener called connect() (the listener reconnects
    // below do): the old receive threads those let go of are never joined.
    TcpClient& client = *new TcpClient();

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

    // --- reconnect from an inline onReceive listener --------------------------
    // Here connect() runs inside processNetwork()'s receive loop, and it sets
    // connected_ again for the new connection before the listener returns. The
    // loop has to see that a newer receive thread took over: otherwise the old
    // thread goes back to recv() on the new socket, next to the new thread and
    // sharing its receive buffer, and never ends.
#ifdef __linux__
    const int threadsBeforeRx = countEntries("/proc/self/task");
#endif
    atomic<bool> rxArmed{true};
    atomic<int> rxListenerResult{-1};   // -1 not run, 0 connect() failed, 1 ok
    EventListener rxReconnectSub = client.onReceive.listen([&](TcpReceiveEventArgs& e) {
        const string data(e.data.begin(), e.data.end());
        if (data.find("reconnect") != string::npos && rxArmed.exchange(false)) {
            rxListenerResult = client.connect("127.0.0.1", port) ? 1 : 0;
        }
    });

    ::send(peer, "reconnect", 9, 0);
    rawsocket_t oldPeer = peer;
    peer = acceptWithin(listener, 3000);
    check("onReceive reconnect: peer accepted the new connection", peer != kNoSocket);
    check("onReceive reconnect: connect() returned true",
          waitFor(3000, [&] { return rxListenerResult.load() == 1; }));
    check("onReceive reconnect: client is connected",
          waitFor(3000, [&] { return client.isConnected(); }));
    rxReconnectSub.disconnect();
    TC_CLOSE(oldPeer);   // the client already closed its end
    if (g_fail) bail();

    check("onReceive reconnect: data reaches the peer", clientToPeer(client, peer, "after rx"));
    check("onReceive reconnect: the client receives the peer's data",
          peerToClient(peer, "pong after the onReceive reconnect"));

#ifdef __linux__
    int threadsAfterRx = -1;
    waitFor(1000, [&] {
        threadsAfterRx = countEntries("/proc/self/task");
        return threadsAfterRx <= threadsBeforeRx;
    });
    printf("  (threads: %d before, %d after)\n", threadsBeforeRx, threadsAfterRx);
    check("onReceive reconnect: the old receive thread stopped",
          threadsAfterRx <= threadsBeforeRx);
#else
    printf("%-60s %s\n", "onReceive reconnect: the old receive thread stopped",
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
    // port is one nothing is bound to (see refusingPort()).
    const int refusedPort = refusingPort();
    check("a refusing port is free", refusedPort != 0);
    if (refusedPort == 0) bail();

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

    // --- a refused connect(), then a reconnect from onError ------------------
    // connect() notified onError and only then closed the failed socket. An
    // onError listener that reconnects has replaced socket_ by then: the
    // close after it returned shut the listener's new connection.
    atomic<bool> refusedArmed{true};
    atomic<int> refusedListenerResult{-1};   // -1 not run, 0 connect() failed, 1 ok
    EventListener refusedErrSub = client.onError.listen([&](TcpErrorEventArgs&) {
        if (refusedArmed.exchange(false)) {
            refusedListenerResult = client.connect("127.0.0.1", port) ? 1 : 0;
        }
    });
    check("onError reconnect: the refused connect() returns false",
          !client.connect("127.0.0.1", refusedPort));
    refusedErrSub.disconnect();
    check("onError reconnect: the listener's connect() returned true",
          refusedListenerResult == 1);
    TC_CLOSE(peer);   // the client closed it when connect() disconnected
    peer = acceptWithin(listener, 2000);
    check("onError reconnect: peer accepted the listener's connection", peer != kNoSocket);
    check("onError reconnect: client is connected", client.isConnected());
    if (g_fail) bail();
    check("onError reconnect: data reaches the peer", clientToPeer(client, peer, "rescued"));
    check("onError reconnect: the client receives the peer's data",
          peerToClient(peer, "glad you made it"));
    if (g_fail) bail();

    // --- without threads: a failed pending connect, then an onError reconnect -
    // With setUseThread(false), connect() is non-blocking and the update event
    // drives processNetwork(), which finds out that the pending connect failed
    // (a loopback connect to the refused port is pending first on Linux). It
    // notified onError and only then disconnected: an onError listener that
    // reconnects had its new pending connect torn down by that disconnect().
    // The test pumps the update event itself, as the app's frame loop would,
    // and prints what it saw, so a failure on a platform we cannot run here
    // says where it stopped.
    {
        TcpClient nt;
        nt.setUseThread(false);
        bool ntArmed = true;
        int ntReconnect = -1;   // -1 not run, 0 connect() failed, 1 ok
        int ntErrors = 0, ntErrorCode = 0;
        string ntErrorMessage;
        EventListener ntErrSub = nt.onError.listen([&](TcpErrorEventArgs& e) {
            ++ntErrors;
            if (ntArmed) {
                ntArmed = false;
                ntErrorCode = e.errorCode;
                ntErrorMessage = e.message;
                ntReconnect = nt.connect("127.0.0.1", port) ? 1 : 0;
            }
        });
        string ntReceived;
        EventListener ntRxSub = nt.onReceive.listen([&](TcpReceiveEventArgs& e) {
            ntReceived.append(e.data.begin(), e.data.end());
        });
        int ticks = 0;
        auto pumpUntil = [&](int ms, auto pred) {
            return waitFor(ms, [&] { ++ticks; events().update.notify(); return pred(); });
        };

        // true when the connect is pending (the processNetwork() path); false
        // where the refusal comes back at once (connect()'s own path)
        const auto started = chrono::steady_clock::now();
        const bool pending = nt.connect("127.0.0.1", refusedPort);
        const int errorsFromConnect = ntErrors;
        printf("  (the refused connect was %s)\n",
               pending ? "pending: processNetwork() reports it" : "refused at once");
        const bool reconnected =
            pumpUntil(5000, [&] { return ntReconnect != -1; }) && ntReconnect == 1;
        const long long reconnectMs = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - started).count();
        printf("  (onError: %d call(s), %d from connect() itself; first: code %d, \"%s\"; "
               "listener's connect(): %d; %d update tick(s), %lld ms)\n",
               ntErrors, errorsFromConnect, ntErrorCode, ntErrorMessage.c_str(),
               ntReconnect, ticks, reconnectMs);
        check("no threads: onError reconnects", reconnected);
        check("no threads: client is connected",
              pumpUntil(3000, [&] { return nt.isConnected(); }));
        ntErrSub.disconnect();
        rawsocket_t ntPeer = acceptWithin(listener, 2000);
        check("no threads: peer accepted the listener's connection", ntPeer != kNoSocket);
        if (g_fail) bail();
        check("no threads: data reaches the peer", clientToPeer(nt, ntPeer, "pumped"));
        ::send(ntPeer, "pong", 4, 0);
        check("no threads: the client receives the peer's data",
              pumpUntil(3000, [&] { return ntReceived == "pong"; }));
        nt.disconnect();
        TC_CLOSE(ntPeer);
        if (g_fail) bail();
    }

    // --- onReceive disconnects, the main thread reconnects --------------------
    // The common "bye" pattern: an onReceive listener calls disconnect() (which
    // lets go of its receive thread) and the app's update, seeing
    // !isConnected(), calls connect(). connect() set running_ / connected_ and
    // notified onConnect before it bumped the generation, so the old thread,
    // back from its listener in that window, saw its own generation still
    // current and went on reading the new socket next to the new receive
    // thread. Here the listener stays until the main thread is inside
    // connect()'s onConnect, which keeps that window open for a while.
    {
        // Kept for the life of the process, as the Events comment of
        // TcpClient asks of a client whose receive-thread listener called
        // disconnect(): its old receive thread is never joined, so destroying
        // the client would race whatever that thread last touched.
        TcpClient& bc = *new TcpClient();
#ifdef __linux__
        const int threadsBaseline = countEntries("/proc/self/task");
#endif
        atomic<bool> byeArmed{true}, byeDisconnected{false}, mainInOnConnect{false};
        mutex bcMutex;
        string bcReceived;
        bool fromOldThread = false;
        EventListener bcRxSub = bc.onReceive.listen([&](TcpReceiveEventArgs& e) {
            const string d(e.data.begin(), e.data.end());
            if (d == "bye" && byeArmed.exchange(false)) {
                t_byeThread = true;
                bc.disconnect();
                byeDisconnected = true;
                waitFor(3000, [&] { return mainInOnConnect.load(); });
                return;
            }
            lock_guard<mutex> lock(bcMutex);
            bcReceived += d;
            if (t_byeThread) fromOldThread = true;
        });
        atomic<bool> holdOnConnect{false};
        EventListener bcConnSub = bc.onConnect.listen([&](TcpConnectEventArgs& e) {
            if (e.success && holdOnConnect.exchange(false)) {
                mainInOnConnect = true;
                // The old thread returns from its listener meanwhile
                this_thread::sleep_for(chrono::milliseconds(200));
            }
        });

        check("bye, reconnect: connect()", bc.connect("127.0.0.1", port));
        rawsocket_t bp = acceptWithin(listener, 2000);
        check("bye, reconnect: peer accepted", bp != kNoSocket);
        if (g_fail) bail();
        ::send(bp, "bye", 3, 0);
        check("bye, reconnect: the onReceive listener disconnected",
              waitFor(3000, [&] { return byeDisconnected.load(); }));
        TC_CLOSE(bp);
        if (g_fail) bail();

        holdOnConnect = true;
        check("bye, reconnect: the main thread's connect()", bc.connect("127.0.0.1", port));
        bp = acceptWithin(listener, 2000);
        check("bye, reconnect: peer accepted the new connection", bp != kNoSocket);
        if (g_fail) bail();
        string expected;
        for (int i = 0; i < 8; ++i) {
            const string m = "m" + to_string(i) + ";";
            expected += m;
            ::send(bp, m.data(), static_cast<int>(m.size()), 0);
            this_thread::sleep_for(chrono::milliseconds(5));
        }
        bool arrived = waitFor(3000, [&] {
            lock_guard<mutex> lock(bcMutex);
            return bcReceived.size() >= expected.size();
        });
        {
            lock_guard<mutex> lock(bcMutex);
            arrived = arrived && bcReceived == expected;
        }
        check("bye, reconnect: the client receives the new peer's data in order", arrived);
        {
            lock_guard<mutex> lock(bcMutex);
            check("bye, reconnect: the old receive thread reads none of it", !fromOldThread);
        }
#ifdef __linux__
        int threadsAfterBye = -1;
        waitFor(1000, [&] {
            threadsAfterBye = countEntries("/proc/self/task");
            return threadsAfterBye <= threadsBaseline + 1;
        });
        printf("  (threads: %d before connect(), %d after; one receive thread expected)\n",
               threadsBaseline, threadsAfterBye);
        check("bye, reconnect: the old receive thread stopped", threadsAfterBye <= threadsBaseline + 1);
#else
        printf("%-60s %s\n", "bye, reconnect: the old receive thread stopped", "SKIP (counted on Linux)");
#endif
        bc.disconnect();
        TC_CLOSE(bp);
        if (g_fail) bail();
    }

    // --- the same, reconnecting without threads ---------------------------------
    // A reconnect with setUseThread(false) did not bump the generation at all,
    // so the old thread, back from its listener, kept driving the new
    // connection (the pending connect, onConnect, reads) next to the update
    // event, for as long as the client lived.
    {
        TcpClient& bn = *new TcpClient();   // kept, as above
#ifdef __linux__
        const int threadsBaseline = countEntries("/proc/self/task");
#endif
        atomic<bool> byeArmed{true}, byeDisconnected{false}, mainReconnected{false};
        EventListener bnRxSub = bn.onReceive.listen([&](TcpReceiveEventArgs& e) {
            const string d(e.data.begin(), e.data.end());
            if (d == "bye" && byeArmed.exchange(false)) {
                t_byeThread = true;
                bn.disconnect();
                byeDisconnected = true;
                waitFor(3000, [&] { return mainReconnected.load(); });
            }
        });
        check("bye, no threads: connect()", bn.connect("127.0.0.1", port));
        rawsocket_t bp = acceptWithin(listener, 2000);
        check("bye, no threads: peer accepted", bp != kNoSocket);
        if (g_fail) bail();
        ::send(bp, "bye", 3, 0);
        check("bye, no threads: the onReceive listener disconnected",
              waitFor(3000, [&] { return byeDisconnected.load(); }));
        TC_CLOSE(bp);
        if (g_fail) bail();

        const thread::id mainThread = this_thread::get_id();
        mutex bnMutex;
        vector<thread::id> connectedOn;
        EventListener bnConnSub = bn.onConnect.listen([&](TcpConnectEventArgs& e) {
            if (e.success) {
                lock_guard<mutex> lock(bnMutex);
                connectedOn.push_back(this_thread::get_id());
            }
        });
        bn.setUseThread(false);
        const bool pendingOrDone = bn.connect("127.0.0.1", port);
        mainReconnected = true;
        check("bye, no threads: the main thread's connect()", pendingOrDone);
        check("bye, no threads: connected, driven by the update event",
              waitFor(3000, [&] { events().update.notify(); return bn.isConnected(); }));
        {
            lock_guard<mutex> lock(bnMutex);
            check("bye, no threads: onConnect fired once, on the main thread",
                  connectedOn.size() == 1 && connectedOn[0] == mainThread);
        }
        bp = acceptWithin(listener, 2000);
        check("bye, no threads: peer accepted the new connection", bp != kNoSocket);
#ifdef __linux__
        int threadsAfterBye = -1;
        waitFor(1000, [&] {
            events().update.notify();
            threadsAfterBye = countEntries("/proc/self/task");
            return threadsAfterBye <= threadsBaseline;
        });
        printf("  (threads: %d before connect(), %d after; no receive thread expected)\n",
               threadsBaseline, threadsAfterBye);
        check("bye, no threads: the old receive thread stopped", threadsAfterBye <= threadsBaseline);
#else
        printf("%-60s %s\n", "bye, no threads: the old receive thread stopped", "SKIP (counted on Linux)");
#endif
        bn.disconnect();
        if (bp != kNoSocket) TC_CLOSE(bp);
        if (g_fail) bail();
    }

    // --- connect() elsewhere while connected, with a listener that reconnects -
    // connect() on a connected client first disconnects, and the listener,
    // which reconnects on every onDisconnect ("Disconnected by client"
    // included), reconnects to the old peer from inside that call. connect()
    // then closes that connection again. When its receive thread was still
    // running there, the shutdown's EOF was reported as a remote close, the
    // listener reconnected again from that thread, and connect() and the
    // thread raced on the same std::thread (join against detach).
    int portB = 0;
    rawsocket_t listenerB = listenLoopback(portB);
    check("connect(B): second listener is up", listenerB != kNoSocket);
    if (g_fail) bail();
    mutex everyMutex;
    vector<string> everyReasons;
    EventListener everySub = client.onDisconnect.listen([&](TcpDisconnectEventArgs& e) {
        {
            lock_guard<mutex> lock(everyMutex);
            everyReasons.push_back(e.reason);
        }
        client.connect("127.0.0.1", port);
    });
#ifdef __linux__
    const int fdsBeforeElsewhere = countEntries("/proc/self/fd");
    const int threadsBeforeElsewhere = countEntries("/proc/self/task");
#endif
    const bool elsewhereOk = client.connect("127.0.0.1", portB);
    everySub.disconnect();
    check("connect(B): connect() returns true", elsewhereOk);
    {
        lock_guard<mutex> lock(everyMutex);
        check("connect(B): one onDisconnect, \"Disconnected by client\"",
              everyReasons.size() == 1 && everyReasons[0] == "Disconnected by client");
    }
    rawsocket_t peerB = acceptWithin(listenerB, 2000);
    check("connect(B): the new peer accepted", peerB != kNoSocket);
    if (g_fail) bail();
    check("connect(B): data reaches the new peer", clientToPeer(client, peerB, "moved"));
    check("connect(B): the client receives the new peer's data",
          peerToClient(peerB, "hello from B"));

    // The listener's connection reached the old peer's listener, and the
    // client has closed it again
    rawsocket_t overruled = acceptWithin(listener, 2000);
    bool overruledClosed = false;
    if (overruled != kNoSocket) {
        setRecvTimeout(overruled, 2000);
        char c;
        overruledClosed = ::recv(overruled, &c, 1, 0) == 0;
        TC_CLOSE(overruled);
    }
    check("connect(B): the listener's connection is closed again",
          overruled != kNoSocket && overruledClosed);
    rawsocket_t extra = acceptWithin(listener, 300);
    check("connect(B): no further reconnect to the old peer", extra == kNoSocket);
    if (extra != kNoSocket) TC_CLOSE(extra);
    TC_CLOSE(peer);   // the old connection, which the client closed
    peer = peerB;
    if (g_fail) bail();
#ifdef __linux__
    int fdsAfterElsewhere = -1, threadsAfterElsewhere = -1;
    waitFor(1000, [&] {
        fdsAfterElsewhere = countEntries("/proc/self/fd");
        threadsAfterElsewhere = countEntries("/proc/self/task");
        return fdsAfterElsewhere <= fdsBeforeElsewhere &&
               threadsAfterElsewhere <= threadsBeforeElsewhere;
    });
    printf("  (descriptors: %d before, %d after; threads: %d before, %d after)\n",
           fdsBeforeElsewhere, fdsAfterElsewhere, threadsBeforeElsewhere, threadsAfterElsewhere);
    check("connect(B): no descriptor or thread left over",
          fdsAfterElsewhere <= fdsBeforeElsewhere && threadsAfterElsewhere <= threadsBeforeElsewhere);
#else
    printf("%-60s %s\n", "connect(B): no descriptor or thread left over",
           "SKIP (counted on Linux)");
#endif
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

    // --- destroying a client with a reconnecting listener attached ----------
    // The destructor disconnects without onDisconnect. This listener reconnects
    // on every onDisconnect, "Disconnected by client" included: told by the
    // destructor, it would reconnect a client that is going away (and leave a
    // joinable receive thread behind: std::terminate). Nor may the receive
    // thread report the EOF of the destructor's own shutdown() (see above).
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
    atomic<int> doomedDisconnects{0};
    TcpClient* doomedPtr = doomed.get();
    EventListener doomedSub = doomed->onDisconnect.listen([&, doomedPtr](TcpDisconnectEventArgs&) {
        ++doomedDisconnects;
        doomedPtr->connect("127.0.0.1", port);
    });
    check("destroyed client: the destructor finishes within 5 s",
          completesWithin(5000, [&] { doomed.reset(); }));
    if (g_fail) bail();
    const bool strayAfterDestroy = strayConnection();
    check("destroyed client: no onDisconnect from the destructor", doomedDisconnects == 0);
    check("destroyed client: no reconnect after destruction", !strayAfterDestroy);
    // doomedSub and doomedRx outlive their Events: disconnecting them is a no-op

    // --- teardown ---------------------------------------------------------
    g_phase = "the scenario's teardown";
    TC_CLOSE(peer);
    TC_CLOSE(listenerB);
    TC_CLOSE(listener);
}

int main() {
#ifdef TC_TEST_CRASH_REPORT
    signal(SIGSEGV, onFatalSignal);
    signal(SIGBUS, onFatalSignal);
    signal(SIGABRT, onFatalSignal);
#endif
    g_phase = "the scenario";
    if (!completesWithin(60000, scenario)) {
        check("scenario finished within 60 s", false);
        bail();
    }
    g_phase = "main(), after the scenario";

    // Nothing may still run when the process tears down its statics: not the
    // scenario's worker (joined), and not a receive thread of the clients the
    // scenario keeps (each has stopped: joined by a disconnect(), or ended on
    // its own once its generation was replaced).
#ifdef __linux__
    int threadsAtExit = -1;
    waitFor(1000, [&] {
        threadsAtExit = countEntries("/proc/self/task");
        return threadsAtExit == 1;
    });
    printf("  (threads when main() returns: %d)\n", threadsAtExit);
    check("no thread is left when main() returns", threadsAtExit == 1);
#else
    printf("%-60s %s\n", "no thread is left when main() returns", "SKIP (counted on Linux)");
#endif
    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    fflush(stdout);   // a crash in static destruction then still shows this
    g_phase = "exit (static destruction)";
    return g_fail ? 1 : 0;
}
