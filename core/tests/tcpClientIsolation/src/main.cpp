// =============================================================================
// core/tests/tcpClientIsolation — regression test for TcpClient's receive
// buffer (#254).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariant: each client's onReceive gets only its own bytes.
// processNetwork() used to receive into a function-local static buffer, one
// for the whole process, which every client's receive thread wrote into at
// the same time: one connection's bytes were delivered to another, and a
// client with a different setReceiveBufferSize() reallocated the buffer under
// the other thread's recv() (heap corruption).
//
// Two raw loopback peers stream 'A' and 'B' to two clients with different
// receive buffer sizes. Pre-fix, foreign bytes show up (or the process
// crashes) almost at once.
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
    #include <poll.h>
    #define TC_CLOSE ::close
    using rawsocket_t = int;
#endif

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__ANDROID__)
    #define TC_TEST_CRASH_REPORT 1
    #include <csignal>
    #include <execinfo.h>
#endif

// The test's own raw sends must not die on SIGPIPE if a client goes away
#if defined(MSG_NOSIGNAL)
    #define TEST_SEND_FLAGS MSG_NOSIGNAL
#else
    #define TEST_SEND_FLAGS 0
#endif

using namespace std;
using namespace tc;

static const rawsocket_t kNoSocket = static_cast<rawsocket_t>(-1);

static atomic<int> g_fail{0};

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
    printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_fail;
}

// Run fn on a worker and report failure if it does not finish in time. A
// worker that finished is joined, so it is not still exiting when main()
// returns and the process tears down its statics; one that hangs is
// detached, and the caller then bails out with _Exit.
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

[[noreturn]] static void bail() {
    printf("\nFAILED\n");
    fflush(stdout);
    _Exit(1);
}

static rawsocket_t listenLoopback(int& port) {
    rawsocket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNoSocket) return kNoSocket;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    socklen_t len = sizeof(addr);
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || ::listen(s, 8) != 0 ||
        ::getsockname(s, (sockaddr*)&addr, &len) != 0) {
        TC_CLOSE(s);
        return kNoSocket;
    }
    port = ntohs(addr.sin_port);
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

// What one client saw, written by its receive thread
struct Tally {
    atomic<size_t> total{0};
    atomic<size_t> foreign{0};
};

static const size_t kBytesPerStream = 8u * 1024u * 1024u;

// Push kBytesPerStream of `fill` to the peer socket
static void pushStream(rawsocket_t s, char fill, const atomic<bool>& go) {
    vector<char> chunk(64 * 1024, fill);
    while (!go.load()) this_thread::yield();
    size_t left = kBytesPerStream;
    while (left > 0) {
        const size_t n = left < chunk.size() ? left : chunk.size();
        int sent = static_cast<int>(::send(s, chunk.data(), static_cast<int>(n), TEST_SEND_FLAGS));
        if (sent <= 0) return;   // the client went away; the totals will say so
        left -= static_cast<size_t>(sent);
    }
}

static void scenario() {
    // Constructed first: they also start Winsock
    TcpClient ca, cb;
    // Different sizes on purpose: pre-fix, each thread resized the shared
    // buffer to its own client's size while the other received into it.
    ca.setReceiveBufferSize(4096);
    cb.setReceiveBufferSize(65536);

    Tally ta, tb;
    EventListener subA = ca.onReceive.listen([&](TcpReceiveEventArgs& e) {
        size_t bad = 0;
        for (char c : e.data) if (c != 'A') ++bad;
        ta.foreign += bad;
        ta.total += e.data.size();
    });
    EventListener subB = cb.onReceive.listen([&](TcpReceiveEventArgs& e) {
        size_t bad = 0;
        for (char c : e.data) if (c != 'B') ++bad;
        tb.foreign += bad;
        tb.total += e.data.size();
    });

    int portA = 0, portB = 0;
    rawsocket_t la = listenLoopback(portA);
    rawsocket_t lb = listenLoopback(portB);
    check("two loopback listeners are up", la != kNoSocket && lb != kNoSocket);
    if (la == kNoSocket || lb == kNoSocket) bail();

    check("client A connects", ca.connect("127.0.0.1", portA));
    check("client B connects", cb.connect("127.0.0.1", portB));
    rawsocket_t pa = acceptWithin(la, 2000);
    rawsocket_t pb = acceptWithin(lb, 2000);
    check("both peers accepted", pa != kNoSocket && pb != kNoSocket);
    if (g_fail) bail();

    // Both receive threads are up; start both streams together
    atomic<bool> go{false};
    thread sa(pushStream, pa, 'A', cref(go));
    thread sb(pushStream, pb, 'B', cref(go));
    go = true;

    const auto deadline = chrono::steady_clock::now() + chrono::seconds(30);
    while (chrono::steady_clock::now() < deadline) {
        if (ta.total.load() >= kBytesPerStream && tb.total.load() >= kBytesPerStream) break;
        if (ta.foreign.load() > 0 || tb.foreign.load() > 0) break;   // already decided
        this_thread::sleep_for(chrono::milliseconds(5));
    }

    printf("  (A: %zu bytes, %zu foreign; B: %zu bytes, %zu foreign)\n",
           ta.total.load(), ta.foreign.load(), tb.total.load(), tb.foreign.load());
    check("client A receives no bytes from B's connection", ta.foreign.load() == 0);
    check("client B receives no bytes from A's connection", tb.foreign.load() == 0);
    check("client A receives exactly its stream", ta.total.load() == kBytesPerStream);
    check("client B receives exactly its stream", tb.total.load() == kBytesPerStream);
    if (g_fail) bail();   // a sender may be stuck on a client that stopped reading

    sa.join();
    sb.join();
    ca.disconnect();
    cb.disconnect();
    TC_CLOSE(pa);
    TC_CLOSE(pb);
    TC_CLOSE(la);
    TC_CLOSE(lb);
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
    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    fflush(stdout);   // a crash in static destruction then still shows this
    g_phase = "exit (static destruction)";
    return g_fail ? 1 : 0;
}
