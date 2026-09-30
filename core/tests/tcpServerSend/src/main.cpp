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
// test process. A crash prints what the test was doing and a backtrace.
// =============================================================================

#include <TrussC.h>

#include <algorithm>
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
    #include <sys/time.h>
    #define TC_CLOSE ::close
    using rawsocket_t = int;
#endif

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__ANDROID__)
    #define TC_TEST_CRASH_REPORT 1
    #include <csignal>
    #include <execinfo.h>
#endif

using namespace std;
using namespace tc;

static int g_fail = 0;

// What the test is doing, for the fatal-signal report below
static const char* volatile g_phase = "starting";

#ifdef TC_TEST_CRASH_REPORT
// A crash prints what the test was doing and a backtrace, then dies of the
// same signal (as in tcpClientReconnect)
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
    printf("%-56s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);   // flush per line so CI logs survive a later hang
    if (!ok) ++g_fail;
}

// Run fn on a worker and report failure if it does not finish in time. A
// worker that finished is joined, so it is not still exiting when main()
// returns and the process tears down its statics; one that hangs is
// detached, and the caller then bails out with _Exit.
//
// When the regression is present, fn never returns, and we still want a clean
// FAIL line plus a non-zero exit rather than a hung process burning the CI job
// timeout. std::async is unusable for this — its future's destructor joins the
// task, so it would block forever on exactly the case under test.
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

// Leave without running destructors. ~TcpServer calls stop(), which joins the
// client threads — that itself deadlocks in the pre-fix build, so a failing run
// must not unwind normally.
[[noreturn]] static void bail() {
    printf("\nFAILED\n");
    fflush(stdout);
    _Exit(1);
}

// Bound a blocking recv() so a drain loop cannot hang once the data runs out.
static void setRecvTimeout(rawsocket_t s, int ms) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

// A peer that connects and then never reads, so the server's socket buffer fills.
static rawsocket_t connectSilentPeer(int port, bool shrinkRecvBuffer = true) {
    rawsocket_t s = ::socket(AF_INET, SOCK_STREAM, 0);

    // Shrink the receive buffer so the sender's queue fills quickly. This has to
    // happen BEFORE connect(): the size takes part in the window negotiation, so
    // setting it on an established socket does not shrink the advertised window.
    //
    // A peer that is meant to DRAIN slowly wants the opposite: the tiny window
    // caps every recv() at a few KB, which would throttle the drain loop far
    // below the rate the test is trying to model.
    if (shrinkRecvBuffer) {
        int rcv = 4096;
        ::setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcv, sizeof(rcv));
    }

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
#ifdef TC_TEST_CRASH_REPORT
    signal(SIGSEGV, onFatalSignal);
    signal(SIGBUS, onFatalSignal);
    signal(SIGABRT, onFatalSignal);
#endif
    const int port = 45871;
    g_phase = "a send parked behind a non-reading peer";

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
    //
    // How much gets through before it stops is a property of the platform too,
    // and it reaches zero: on macOS the very first chunk blocks against a peer
    // with a 4 KB receive buffer, so "parked" cannot require that a chunk was
    // completed first.
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

    // Blocked == no progress for a while while the send is still running. The
    // send thread sets sendReturned when send() gives up or finishes, so a
    // still-clear flag is what says the silence is a parked send rather than a
    // finished one — a chunk counter that never leaves 0 says the same thing.
    bool parked = false;
    long long lastSeen = -1;
    int quietRounds = 0;
    for (int i = 0; i < 100 && !parked; ++i) {
        this_thread::sleep_for(chrono::milliseconds(100));
        const long long now = chunksSent.load();
        if (sendReturned.load()) break;
        quietRounds = (now == lastSeen) ? quietRounds + 1 : 0;
        lastSeen = now;
        if (!sendReturned.load() && quietRounds >= 4) parked = true;   // ~400 ms of silence
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
    g_phase = "disconnecting the client with the parked send";
    // Print what it measured. The waits inside send() and the receive thread
    // re-check on a 100 ms slice, so a disconnect that lands well under that is
    // being woken by shutdown() rather than waiting for the next check --
    // which is true on Linux and macOS and not on Windows (see kWaitSliceMs).
    const auto disconnectStart = chrono::steady_clock::now();
    const bool disconnected = completesWithin(4000, [&] { server.disconnectClient(stalledId); });
    printf("  (disconnectClient took %.0f ms; the waits re-check every 100 ms)\n",
           chrono::duration<double, milli>(chrono::steady_clock::now() - disconnectStart).count());
    check("disconnecting the stalled client unblocks its send", disconnected);

    if (g_fail) { blocker.detach(); bail(); }
    if (blocker.joinable()) blocker.join();
    if (parked) check("the parked send returned after disconnect", sendReturned.load());

    if (healthy != static_cast<rawsocket_t>(-1)) TC_CLOSE(healthy);
    TC_CLOSE(stalled);

    const bool stopped = completesWithin(4000, [&] { server.stop(); });
    check("server stops cleanly", stopped);
    if (!stopped) bail();

    // --- onError must not run while the send lock is held --------------------
    // Disconnecting the offending client is the obvious thing to write in an
    // onError listener. Listeners run inline on the sending thread by default,
    // so firing the event under the send mutex made that handler re-enter the
    // same non-recursive mutex through closeChannel() and wedge the caller.
    //
    // Getting there is the premise, not the invariant: the listener runs only
    // once a send has filled the socket buffers and then sat idle past the
    // timeout, and how much data and time that takes depends on the platform's
    // buffers and on how busy the machine is (a fixed 15 s limit on the whole
    // sequence failed once on a loaded machine). So the wait for the send to
    // block has no fixed limit, only one on going without progress, and the
    // deadline applies from the moment the listener is entered: the listener
    // and the send it cut short must then finish.
    g_phase = "an onError listener disconnecting its own client";
    {
        TcpServer s2;
        if (!s2.start(port + 1, 8)) { printf("could not start second server\n"); bail(); }
        s2.setSendTimeout(0.5f);

        auto entered = make_shared<atomic<bool>>(false);
        auto handled = make_shared<atomic<bool>>(false);
        EventListener sub = s2.onError.listen([&s2, entered, handled](TcpServerErrorEventArgs& e) {
            entered->store(true);
            s2.disconnectClient(e.clientId);   // re-enters the send path's mutex
            handled->store(true);
        });

        rawsocket_t deaf = connectSilentPeer(port + 1);
        if (deaf == static_cast<rawsocket_t>(-1)) { printf("no peer\n"); bail(); }
        for (int i = 0; i < 200 && s2.getClientCount() < 1; ++i)
            this_thread::sleep_for(chrono::milliseconds(5));

        vector<int> ids2 = s2.getClientIds();
        if (ids2.empty()) { printf("no client id (2)\n"); bail(); }
        const int deafId = ids2[0];

        // Send until a send blocks. One 8 MB payload is enough on Linux and
        // macOS, but Winsock may take a multi-megabyte payload whole. Once the
        // listener has dropped the client, send() returns false and this stops.
        vector<char> big(8u * 1024u * 1024u, 'y');
        auto sendsThrough = make_shared<atomic<int>>(0);
        auto senderDone = make_shared<atomic<bool>>(false);
        thread sender([&s2, &big, deafId, sendsThrough, senderDone] {
            while (s2.send(deafId, big.data(), big.size())) {
                if (sendsThrough->fetch_add(1) + 1 >= 64) break;   // 512 MB: give up, not our bug
            }
            senderDone->store(true);
        });

        // Wait for the send to block and time out; the listener being entered
        // is what says it did. A payload that goes through whole is progress
        // and restarts the wait. 30 s without either is 60 times the idle
        // timeout: the send is stuck without timing out.
        const auto t0 = chrono::steady_clock::now();
        auto lastProgress = t0;
        int lastThrough = 0;
        while (!entered->load() && !senderDone->load()) {
            const auto now = chrono::steady_clock::now();
            const int through = sendsThrough->load();
            if (through != lastThrough) {
                lastThrough = through;
                lastProgress = now;
            }
            if (now - lastProgress > chrono::seconds(30)) break;
            this_thread::sleep_for(chrono::milliseconds(5));
        }
        const double blockSecs = chrono::duration<double>(chrono::steady_clock::now() - t0).count();

        if (entered->load()) {
            printf("  (the send blocked and timed out after %.2fs, %d whole payload(s) through first)\n",
                   blockSecs, sendsThrough->load());
            const auto t1 = chrono::steady_clock::now();
            const bool finished = waitFor(10000, [&] { return handled->load() && senderDone->load(); });
            printf("  (the listener and the send %s %.0f ms after the listener was entered)\n",
                   finished ? "returned" : "had not returned",
                   chrono::duration<double, milli>(chrono::steady_clock::now() - t1).count());
            check("onError listener may disconnect its own client", finished);
        } else if (senderDone->load()) {
            // The premise is not the invariant: a platform that swallowed every
            // payload never ran the listener, which says nothing either way.
            printf("%-56s %s\n", "onError listener may disconnect its own client",
                   "SKIP (no send blocked: every payload went through)");
            fflush(stdout);
        } else {
            printf("  (no progress and no onError for 30 s; %.2fs since the first send)\n", blockSecs);
            check("a send to a non-reading peer times out into onError", false);
        }

        if (g_fail) {
            sender.detach();
            bail();
        }
        sender.join();

        TC_CLOSE(deaf);
        if (g_fail) bail();
        const bool s2Stopped = completesWithin(4000, [&] { s2.stop(); });
        check("second server stops cleanly", s2Stopped);
        if (!s2Stopped) bail();
    }

    // --- the timeout measures silence, not the length of the send -----------
    g_phase = "a slow but draining peer";
    // A peer that drains a little at a time keeps the send progressing, so a
    // payload that takes far longer than the timeout to deliver must still go
    // through. Measuring total elapsed time instead would drop a healthy client
    // for the offence of being on a slow link with a big payload.
    {
        TcpServer s3;
        if (!s3.start(port + 2, 8)) { printf("could not start third server\n"); bail(); }
        s3.setSendTimeout(1.0f);

        rawsocket_t slow = connectSilentPeer(port + 2, /*shrinkRecvBuffer=*/false);
        if (slow == static_cast<rawsocket_t>(-1)) { printf("no slow peer\n"); bail(); }
        setRecvTimeout(slow, 200);
        for (int i = 0; i < 200 && s3.getClientCount() < 1; ++i)
            this_thread::sleep_for(chrono::milliseconds(5));

        vector<int> ids3 = s3.getClientIds();
        if (ids3.empty()) { printf("no client id (3)\n"); bail(); }

        // 16 MB is past what a sender-side socket buffer absorbs outright, so
        // the send has to wait on the peer and the wait is what gets measured.
        atomic<bool> sendOk{false}, sendDone{false};
        vector<char> payload(16u * 1024u * 1024u, 'z');
        const auto sendStart = chrono::steady_clock::now();
        thread sender([&] {
            sendOk = s3.send(ids3[0], payload.data(), payload.size());
            sendDone = true;
        });

        // 256 KB every 40 ms (~6 MB/s): about 2.5 s to take 16 MB, well past the
        // 1 s timeout, but never 40 ms without progress.
        vector<char> sink(256u * 1024u);
        const auto deadline = chrono::steady_clock::now() + chrono::seconds(30);
        while (!sendDone.load() && chrono::steady_clock::now() < deadline) {
            (void)::recv(slow, sink.data(), static_cast<int>(sink.size()), 0);
            this_thread::sleep_for(chrono::milliseconds(40));
        }
        if (sender.joinable()) sender.join();
        const double sendSecs = chrono::duration<double>(chrono::steady_clock::now() - sendStart).count();

        // The premise is not the invariant: if the platform swallowed the whole
        // payload faster than the timeout, the send was never at risk and the
        // check would pass without exercising anything.
        //
        // Windows reports SKIP here and cannot be tuned out of it. Winsock's
        // send buffering is not bounded by the peer's advertised window, so
        // shrinking that window does not make it wait -- measured, it makes it
        // worse (16 MB absorbed in 50 ms against a default window, 10 ms against
        // a 64 KB one, while macOS slowed from 8.4 s to 13.7 s because the small
        // window also caps what each recv() returns). The only lever is
        // SO_SNDBUF on the server's own socket, which is not the test's to set.
        //
        // Leaving it at SKIP is sound: what this case guards is that our own
        // deadline restarts on progress, which is arithmetic in send(), not
        // platform behaviour. macOS and Linux both reach the state and check it.
        printf("  (the send took %.2fs against a 1.00s idle timeout)\n", sendSecs);
        if (sendSecs <= 1.0) {
            printf("%-56s %s\n", "a slow but draining peer does not trip the idle timeout",
                   "SKIP (payload absorbed faster than the timeout)");
            fflush(stdout);
        } else {
            check("a slow but draining peer does not trip the idle timeout", sendOk.load());
        }

        TC_CLOSE(slow);
        if (g_fail) bail();
        const bool s3Stopped = completesWithin(4000, [&] { s3.stop(); });
        check("third server stops cleanly", s3Stopped);
        if (!s3Stopped) bail();
    }

    // --- teardown waits for the client threads it started ---------------------
    g_phase = "stop() with a listener still running";
    // stop() used to detach every client thread instead of joining it, so it
    // returned — and ~TcpServer() finished — while those threads were still
    // reading members of the object being destroyed.
    //
    // Timing is what separates the two: an onReceive listener runs ON the
    // client's receive thread, so a listener that is still busy when stop() is
    // called holds that thread. Joining waits for it; detaching does not. A
    // stop() that returns while the listener is mid-call is the bug, and it is
    // observable without a sanitizer. The listener says when it is done, so the
    // check reads that rather than how long stop() took: on a busy machine the
    // listener can be well into its 600 ms before stop() even starts.
    {
        TcpServer s5;
        if (!s5.start(port + 4, 8)) { printf("could not start fifth server\n"); bail(); }

        auto entered = make_shared<atomic<bool>>(false);
        auto left = make_shared<atomic<bool>>(false);
        EventListener busy = s5.onReceive.listen([entered, left](TcpServerReceiveEventArgs&) {
            entered->store(true);
            this_thread::sleep_for(chrono::milliseconds(600));
            left->store(true);
        });

        rawsocket_t talker = connectSilentPeer(port + 4);
        if (talker == static_cast<rawsocket_t>(-1)) { printf("no talker\n"); bail(); }
        for (int i = 0; i < 200 && s5.getClientCount() < 1; ++i)
            this_thread::sleep_for(chrono::milliseconds(5));
        (void)::send(talker, "x", 1, 0);
        for (int i = 0; i < 400 && !entered->load(); ++i)
            this_thread::sleep_for(chrono::milliseconds(5));
        check("the listener is running on the client thread", entered->load());

        const bool busyAtStop = !left->load();
        bool leftAtReturn = false;
        const auto t0 = chrono::steady_clock::now();
        const bool returned = completesWithin(10000, [&] {
            s5.stop();
            leftAtReturn = left->load();
        });
        const double stopMs = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
        printf("  (stop() took %.0f ms against a listener busy for 600 ms)\n", stopMs);
        check("stop() returns rather than hanging", returned);
        if (!returned) { bail(); }
        if (busyAtStop) {
            check("stop() waits for a client thread still inside a listener", leftAtReturn);
        } else {
            printf("%-56s %s\n", "stop() waits for a client thread still inside a listener",
                   "SKIP (the listener finished before stop() was called)");
            fflush(stdout);
        }

        TC_CLOSE(talker);
    }

    // --- sendAsync() returns without waiting for the peer ---------------------
    g_phase = "sendAsync() to a non-reading peer";
    // The request this whole thing started from: a send issued from a draw loop
    // to a peer that has stopped reading has to return in microseconds. The
    // synchronous send() cannot — it is done only once the kernel has the
    // payload — so it stalls the frame however well the rest of the server
    // holds up.
    //
    // The same block covers the accounting the queue owes its caller: what is
    // outstanding, what the high-water mark refuses, and that every id it
    // handed out is reported back exactly once even when the server stops with
    // the queue still full.
    {
        TcpServer s6;
        if (!s6.start(port + 5, 8)) { printf("could not start sixth server\n"); bail(); }

        auto completedMutex = make_shared<mutex>();
        auto completed = make_shared<vector<TcpSendCompleteEventArgs>>();
        EventListener finished = s6.onSendComplete.listen(
            [completedMutex, completed](TcpSendCompleteEventArgs& a) {
                lock_guard<mutex> lock(*completedMutex);
                completed->push_back(a);
            });

        rawsocket_t deaf = connectSilentPeer(port + 5);
        if (deaf == static_cast<rawsocket_t>(-1)) { printf("no deaf peer\n"); bail(); }
        for (int i = 0; i < 200 && s6.getClientCount() < 1; ++i)
            this_thread::sleep_for(chrono::milliseconds(5));
        vector<int> ids6 = s6.getClientIds();
        if (ids6.empty()) { printf("deaf peer never registered\n"); bail(); }
        const int deafId = ids6[0];

        // Four of these fit under the 16 MB default mark; none of them can be
        // written, because the peer never reads.
        vector<char> big(4 * 1024 * 1024, 'x');
        vector<uint64_t> queuedIds;
        double worstMs = 0.0;
        for (int i = 0; i < 4; ++i) {
            const auto t0 = chrono::steady_clock::now();
            const SendResult r = s6.sendAsync(deafId, big.data(), big.size());
            const double ms =
                chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
            worstMs = max(worstMs, ms);
            if (r) queuedIds.push_back(r.id);
        }
        printf("  (slowest of 4 sendAsync calls to a non-reading peer took %.1f ms)\n", worstMs);
        check("sendAsync queues every payload up to the mark", queuedIds.size() == 4);
        check("sendAsync returns without waiting for the peer", worstMs < 500.0);
        check("pending bytes account for what is queued",
              s6.getSendAsyncPendingBytes(deafId) > 4u * 1024 * 1024);
        check("pending bytes are 0 for an unknown client", s6.getSendAsyncPendingBytes(9999) == 0);

        // The limit is a high-water mark, not a cap: it refuses a send only once
        // the queue ALREADY holds that much, and it does not drop the client.
        s6.setSendAsyncBufferSize(1024 * 1024);
        check("getSendAsyncBufferSize() reports what was set",
              s6.getSendAsyncBufferSize() == 1024u * 1024);
        const SendResult refused = s6.sendAsync(deafId, big.data(), big.size());
        check("sendAsync is refused once the queue is past its mark",
              refused.error == SendError::QueueFull);
        check("a refused send is falsy and has no id", !refused && refused.id == 0);
        check("a full queue does not disconnect the client", s6.getClientCount() == 1);

        const bool s6Stopped = completesWithin(10000, [&] { s6.stop(); });
        check("sixth server stops cleanly", s6Stopped);
        if (!s6Stopped) bail();

        // stop() joins the writer, so every completion has already fired.
        {
            lock_guard<mutex> lock(*completedMutex);
            check("every queued send completed", completed->size() == queuedIds.size());
            bool onceEach = completed->size() == queuedIds.size();
            for (size_t i = 0; i < queuedIds.size() && onceEach; ++i) {
                size_t seen = 0;
                for (const auto& c : *completed)
                    if (c.sendId == queuedIds[i]) ++seen;
                if (seen != 1) onceEach = false;
            }
            check("each queued id completed exactly once", onceEach);

            // What the teardown could not write has to come back as
            // Disconnected. How MUCH it could not write is a premise, not the
            // invariant, and it is not the same everywhere: Winsock accepts a
            // multi-megabyte payload whole — it locks the caller's pages and
            // sends in the background rather than copying into a socket buffer
            // the peer's window bounds — so on Windows the first of these is
            // written in full and completes as a success, while Linux cannot
            // place any of it. (Measured on Windows 11 / MSVC: payload 0
            // accepted by a single send() in 0.3 ms, payloads 1-3 blocked.)
            //
            // So pair each completion with its own bytesSent instead of
            // assuming the platform: whatever got out whole must report
            // success, whatever did not must report Disconnected.
            bool consistent = true;
            size_t unwritten = 0;
            for (const auto& c : *completed) {
                const bool whole = c.bytesSent == big.size();
                if (whole && c.error != SendError::None) consistent = false;
                if (!whole && c.error != SendError::Disconnected) consistent = false;
                if (c.error == SendError::Disconnected) ++unwritten;
            }
            printf("  (%zu of %zu queued sends were still unwritten at teardown)\n",
                   unwritten, queuedIds.size());
            check("completion error matches how much of the payload got out", consistent);
        }

        TC_CLOSE(deaf);
    }

    // --- one queue per client, shared by send() and sendAsync() ---------------
    g_phase = "send() and sendAsync() ordering";
    // send() is sendAsync() plus a wait on that one id. Sharing the queue is
    // what keeps the two in order: a synchronous send that wrote directly to the
    // socket would overtake everything already queued ahead of it.
    {
        TcpServer s7;
        if (!s7.start(port + 6, 8)) { printf("could not start seventh server\n"); bail(); }

        rawsocket_t reader = connectSilentPeer(port + 6, /*shrinkRecvBuffer=*/false);
        if (reader == static_cast<rawsocket_t>(-1)) { printf("no reader\n"); bail(); }
        setRecvTimeout(reader, 2000);
        for (int i = 0; i < 200 && s7.getClientCount() < 1; ++i)
            this_thread::sleep_for(chrono::milliseconds(5));
        vector<int> ids7 = s7.getClientIds();
        if (ids7.empty()) { printf("reader never registered\n"); bail(); }
        const int readerId = ids7[0];

        s7.sendAsync(readerId, string("A"));
        s7.sendAsync(readerId, string("B"));
        s7.send(readerId, string("C"));            // sync, must not overtake A and B
        s7.sendAsync(readerId, string("D"));
        vector<char> tail{'E'};
        s7.sendAsync(readerId, move(tail));       // the move overload

        string got;
        while (got.size() < 5) {
            char buf[16];
            const int n = static_cast<int>(::recv(reader, buf, sizeof(buf), 0));
            if (n <= 0) break;
            got.append(buf, buf + n);
        }
        check("sync and async sends to one client arrive in order", got == "ABCDE");

        // broadcastAsync buffers the payload once and reports how many clients
        // took it.
        rawsocket_t second = connectSilentPeer(port + 6, /*shrinkRecvBuffer=*/false);
        if (second == static_cast<rawsocket_t>(-1)) { printf("no second reader\n"); bail(); }
        setRecvTimeout(second, 2000);
        for (int i = 0; i < 200 && s7.getClientCount() < 2; ++i)
            this_thread::sleep_for(chrono::milliseconds(5));

        const int queuedFor = s7.broadcastAsync(string("hi"));
        check("broadcastAsync reports how many clients it queued for", queuedFor == 2);

        string a, b;
        for (rawsocket_t peer : {reader, second}) {
            string& into = (peer == reader) ? a : b;
            while (into.size() < 2) {
                char buf[8];
                const int n = static_cast<int>(::recv(peer, buf, sizeof(buf), 0));
                if (n <= 0) break;
                into.append(buf, buf + n);
            }
        }
        check("broadcastAsync reaches every client", a == "hi" && b == "hi");

        const bool s7Stopped = completesWithin(4000, [&] { s7.stop(); });
        check("seventh server stops cleanly", s7Stopped);
        if (!s7Stopped) bail();

        TC_CLOSE(reader);
        TC_CLOSE(second);
    }

    // --- a timeout that wrote nothing is not a disconnect ---------------------
    g_phase = "a timeout that wrote nothing";
    // The completion has to say which of two very different things happened:
    // the peer is gone, or the peer is merely too slow and the connection is
    // still there. Reporting both as Disconnected left a listener parsing the
    // onError message string to tell them apart.
    //
    // Constructing "timed out having written nothing" on demand needs a message
    // the kernel cannot take half of. One byte is that message: a buffer
    // boundary then always falls on a message boundary, so each send is either
    // written whole or not at all. Fill the pipe a byte at a time and the first
    // send after it fills is the one that gets nothing through.
    {
        TcpServer s8;
        if (!s8.start(port + 7, 8)) { printf("could not start eighth server\n"); bail(); }
        s8.setSendTimeout(0.5f);                  // short, so this does not take a minute
        s8.setSendAsyncBufferSize(64 * 1024);     // bounds the queue at 64k one-byte items

        auto sawTimeout = make_shared<atomic<bool>>(false);
        auto sawTimeoutBytes = make_shared<atomic<size_t>>(1);
        EventListener finished = s8.onSendComplete.listen(
            [sawTimeout, sawTimeoutBytes](TcpSendCompleteEventArgs& a) {
                if (a.error != SendError::Timeout) return;
                sawTimeoutBytes->store(a.bytesSent);
                sawTimeout->store(true);
            });

        rawsocket_t deaf = connectSilentPeer(port + 7);
        if (deaf == static_cast<rawsocket_t>(-1)) { printf("no deaf peer\n"); bail(); }
        for (int i = 0; i < 200 && s8.getClientCount() < 1; ++i)
            this_thread::sleep_for(chrono::milliseconds(5));
        vector<int> ids8 = s8.getClientIds();
        if (ids8.empty()) { printf("deaf peer never registered\n"); bail(); }
        const int deafId = ids8[0];

        const auto t0 = chrono::steady_clock::now();
        const auto deadline = t0 + chrono::seconds(30);
        uint64_t offered = 0;
        while (!sawTimeout->load() && chrono::steady_clock::now() < deadline) {
            for (int i = 0; i < 4096 && !sawTimeout->load(); ++i) {
                ++offered;
                if (!s8.sendAsync(deafId, string("x"))) break;   // queue full: let it drain
            }
            this_thread::sleep_for(chrono::milliseconds(1));
        }
        const double secs = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
        printf("  (%llu one-byte sends in %.1fs before the pipe stopped taking them)\n",
               static_cast<unsigned long long>(offered), secs);

        // The premise is not the invariant. A platform that keeps swallowing
        // one-byte sends for thirty seconds has not disproved anything.
        if (!sawTimeout->load()) {
            printf("%-56s %s\n", "a send that timed out having written nothing reports Timeout",
                   "SKIP (the pipe never stopped accepting bytes)");
            fflush(stdout);
        } else {
            check("a send that timed out having written nothing reports Timeout", true);
            check("that timeout reports no bytes sent", sawTimeoutBytes->load() == 0);
            check("a pure timeout leaves the client connected", s8.getClientCount() == 1);
        }
        const bool s8Stopped = completesWithin(15000, [&] { s8.stop(); });
        check("eighth server stops cleanly", s8Stopped);
        if (!s8Stopped) bail();

        TC_CLOSE(deaf);
    }

    // --- the same teardown, repeatedly, to shake out a deadlock ---------------
    g_phase = "repeated teardown with live clients";
    // Joining is the kind of fix that fails loudly in the other direction: a
    // server torn down while a client thread holds, or waits for, the same lock
    // hangs instead of returning. Churn through it.
    {
        const bool ok = completesWithin(30000, [&] {
            for (int round = 0; round < 20; ++round) {
                TcpServer s4;
                if (!s4.start(port + 3, 8)) return;

                rawsocket_t a = connectSilentPeer(port + 3);
                rawsocket_t b = connectSilentPeer(port + 3);
                for (int i = 0; i < 200 && s4.getClientCount() < 2; ++i)
                    this_thread::sleep_for(chrono::milliseconds(5));

                // Leave traffic in flight so the receive threads are awake, and
                // one client mid-send, when the destructor runs.
                for (int id : s4.getClientIds()) s4.send(id, string("ping"));

                if (a != static_cast<rawsocket_t>(-1)) TC_CLOSE(a);
                if (b != static_cast<rawsocket_t>(-1)) TC_CLOSE(b);
            }                                   // ~TcpServer() -> stop() here
        });
        check("destroying a server with live clients joins its threads", ok);
        if (!ok) bail();                        // a hang here means the join deadlocked
    }

    g_phase = "main(), after the checks";
    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    fflush(stdout);   // a crash in static destruction then still shows this
    g_phase = "exit (static destruction)";
    return g_fail ? 1 : 0;
}
