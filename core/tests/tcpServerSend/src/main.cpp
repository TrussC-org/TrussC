// =============================================================================
// core/tests/tcpServerSend — behavioral regression test for TcpServer::send.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariant: a client that stops reading stalls ONLY its own send.
// TcpServer::send() used to hold clientsMutex_ across the whole blocking send
// loop, and that same mutex guards client registration, disconnects and every
// other send — so one unresponsive peer froze the entire server
// (head-of-line blocking). Each client now owns its own send lock.
//
// The pre-fix build does not fail these checks, it HANGS on them, so every
// assertion runs on a worker with a wait_for() deadline instead of blocking the
// test process.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
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
    #define TC_CLOSE ::close
    using rawsocket_t = int;
#endif

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-56s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);   // flush per line so CI logs survive a later hang
    if (!ok) ++g_fail;
}

// Run fn on a worker and report failure if it does not finish in time.
//
// The thread is DETACHED and the flag is shared, deliberately: when the
// regression is present, fn never returns, and we still want a clean FAIL line
// plus a non-zero exit rather than a hung process burning the CI job timeout.
// std::async is unusable for this — its future's destructor joins the task, so
// it would block forever on exactly the case under test.
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

// Leave without running destructors. ~TcpServer calls stop(), which joins the
// client threads — that itself deadlocks in the pre-fix build, so a failing run
// must not unwind normally.
[[noreturn]] static void bail() {
    printf("\nFAILED\n");
    fflush(stdout);
    _Exit(1);
}

// A peer that connects and then never reads, so the server's socket buffer fills.
static rawsocket_t connectSilentPeer(int port) {
    rawsocket_t s = ::socket(AF_INET, SOCK_STREAM, 0);

    // Shrink the receive buffer so the sender's queue fills quickly. This has to
    // happen BEFORE connect(): the size takes part in the window negotiation, so
    // setting it on an established socket does not shrink the advertised window.
    int rcv = 4096;
    ::setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcv, sizeof(rcv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        TC_CLOSE(s);
        return static_cast<rawsocket_t>(-1);
    }
    return s;
}

int main() {
    const int port = 45871;

    TcpServer server;
    if (!server.start(port, 8)) {
        printf("could not start server on port %d\n", port);
        return 1;
    }

    // --- a peer that never reads -------------------------------------------
    rawsocket_t stalled = connectSilentPeer(port);
    check("stalled peer connected", stalled != static_cast<rawsocket_t>(-1));
    if (stalled == static_cast<rawsocket_t>(-1)) return 1;

    // Wait for the accept loop to register it
    for (int i = 0; i < 200 && server.getClientCount() < 1; ++i)
        this_thread::sleep_for(chrono::milliseconds(5));
    check("stalled peer registered", server.getClientCount() == 1);

    vector<int> ids = server.getClientIds();
    if (ids.empty()) { printf("no client id\n"); return 1; }
    const int stalledId = ids[0];

    // --- park a send inside the kernel by overflowing the stalled peer ------
    //
    // How much has to be pushed before send() blocks is entirely a property of
    // the platform's socket buffers (Winsock in particular keeps its own send
    // buffer and returns as soon as the payload is copied into it), so a fixed
    // payload size is not a portable way to reach the state under test. Send
    // 1 MB chunks in a loop instead and watch for progress to stop.
    atomic<bool> sendReturned{false};
    atomic<long long> chunksSent{0};
    vector<char> chunk(1u * 1024u * 1024u, 'x');
    thread blocker([&] {
        while (server.send(stalledId, chunk.data(), chunk.size())) {
            chunksSent.fetch_add(1);
            if (chunksSent.load() > 512) break;   // 512 MB: give up, not our bug
        }
        sendReturned = true;
    });

    // Blocked == no progress for a while, with at least something written.
    bool parked = false;
    long long lastSeen = -1;
    int quietRounds = 0;
    for (int i = 0; i < 100 && !parked; ++i) {
        this_thread::sleep_for(chrono::milliseconds(100));
        const long long now = chunksSent.load();
        if (sendReturned.load()) break;
        quietRounds = (now == lastSeen) ? quietRounds + 1 : 0;
        lastSeen = now;
        if (now > 0 && quietRounds >= 4) parked = true;   // ~400 ms of silence
    }

    // The premise is not the invariant. If this platform will not let us wedge a
    // send, say so and still check that nothing else regressed, rather than
    // reporting a failure that says nothing about the code under test.
    if (!parked) {
        printf("%-56s %s\n", "send to a non-reading peer is still in flight",
               "SKIP (could not wedge a send on this platform)");
        fflush(stdout);
    } else {
        check("send to a non-reading peer is still in flight", !sendReturned.load());
    }

    // --- the actual invariant: the rest of the server keeps working ---------
    check("getClientIds() does not block behind that send",
          completesWithin(2000, [&] { (void)server.getClientIds(); }));

    check("getClientCount() does not block behind that send",
          completesWithin(2000, [&] { (void)server.getClientCount(); }));

    bool accepted = false, sentToHealthy = false;
    rawsocket_t healthy = static_cast<rawsocket_t>(-1);
    check("a second client can still connect and be served",
          completesWithin(4000, [&] {
              healthy = connectSilentPeer(port);
              if (healthy == static_cast<rawsocket_t>(-1)) return;
              for (int i = 0; i < 400 && server.getClientCount() < 2; ++i)
                  this_thread::sleep_for(chrono::milliseconds(5));
              accepted = server.getClientCount() == 2;
              if (!accepted) return;
              for (int id : server.getClientIds()) {
                  if (id == stalledId) continue;
                  // Small payload, a peer that is not backed up: must go through
                  sentToHealthy = server.send(id, string("ping"));
              }
          }));
    check("second client was accepted while the first was stalled", accepted);
    check("send to the healthy client succeeded", sentToHealthy);

    // Past this point the test tears the server down, which only terminates if
    // the invariant above holds. Bail out instead of deadlocking on cleanup.
    if (g_fail) {
        blocker.detach();
        bail();
    }

    // --- teardown must not hang behind the parked send ----------------------
    check("disconnecting the stalled client unblocks its send",
          completesWithin(4000, [&] { server.disconnectClient(stalledId); }));

    if (g_fail) { blocker.detach(); bail(); }
    if (blocker.joinable()) blocker.join();
    if (parked) check("the parked send returned after disconnect", sendReturned.load());

    if (healthy != static_cast<rawsocket_t>(-1)) TC_CLOSE(healthy);
    TC_CLOSE(stalled);

    check("server stops cleanly", completesWithin(4000, [&] { server.stop(); }));

    // --- onError must not run while the send lock is held --------------------
    // Disconnecting the offending client is the obvious thing to write in an
    // onError listener. Listeners run inline on the sending thread by default,
    // so firing the event under the send mutex made that handler re-enter the
    // same non-recursive mutex through closeChannel() and wedge the caller.
    {
        TcpServer s2;
        if (!s2.start(port + 1, 8)) { printf("could not start second server\n"); bail(); }
        s2.setSendTimeout(0.5f);

        auto handled = make_shared<atomic<bool>>(false);
        EventListener sub = s2.onError.listen([&s2, handled](TcpServerErrorEventArgs& e) {
            s2.disconnectClient(e.clientId);   // re-enters the send path's mutex
            handled->store(true);
        });

        rawsocket_t deaf = connectSilentPeer(port + 1);
        if (deaf == static_cast<rawsocket_t>(-1)) { printf("no peer\n"); bail(); }
        for (int i = 0; i < 200 && s2.getClientCount() < 1; ++i)
            this_thread::sleep_for(chrono::milliseconds(5));

        vector<int> ids2 = s2.getClientIds();
        if (ids2.empty()) { printf("no client id (2)\n"); bail(); }

        vector<char> big(8u * 1024u * 1024u, 'y');
        check("onError listener may disconnect its own client",
              completesWithin(15000, [&] { s2.send(ids2[0], big.data(), big.size()); }));

        TC_CLOSE(deaf);
        if (g_fail) bail();
        check("second server stops cleanly", completesWithin(4000, [&] { s2.stop(); }));
    }

    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
