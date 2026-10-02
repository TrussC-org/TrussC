// =============================================================================
// core/tests/tcpClientSigpipe — regression test for TcpClient::send() to a
// peer that already closed the connection (#254).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariant: on macOS / Linux, send() to a peer that reset the
// connection returns false; it does not raise SIGPIPE, whose default action
// kills the process without a trace. TcpClient sent without MSG_NOSIGNAL and
// never set SO_NOSIGPIPE, so a send in the window between the peer's reset and
// the receive thread clearing connected_ ended the process on signal 13.
// TcpServer already had both.
//
// Part 1 closes that window deterministically: with setUseThread(false) and
// nobody calling processNetwork(), nothing clears connected_, so every send
// reaches the dead socket. On Linux the first one reports ECONNRESET and the
// second EPIPE, which is where the pre-fix build dies. Part 2 is the threaded
// race as it happens in an app (an MQTT broker restarting mid-publish).
//
// POSIX only: Windows has no SIGPIPE.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>

#ifdef _WIN32

TC_CORE_TEST_MAIN() {
    printf("SKIP: SIGPIPE does not exist on Windows\n");
    return 0;
}

#else

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__ANDROID__)
    #define TC_TEST_CRASH_REPORT 1
    #include <execinfo.h>
#endif

using namespace std;
using namespace tc;

namespace {

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
    fflush(stdout);   // flush per line: the pre-fix build dies on a signal
    if (!ok) ++g_fail;
}

// Run fn on a worker and report failure if it does not finish in time. A
// worker that finished is joined, so it is not still exiting when main()
// returns and the process tears down its statics; one that hangs is
// detached, and the caller then bails out with _Exit.
template <typename F>
static bool completesWithin(int ms, F fn) {
    auto done = make_shared<atomic<bool>>(false);
    thread worker([done, fn = std::move(fn)]() mutable { fn(); done->store(true); });
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

template <typename P>
static bool waitFor(int ms, P pred) {
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        this_thread::sleep_for(chrono::milliseconds(2));
    }
    return pred();
}

[[noreturn]] static void bail() {
    printf("\nFAILED\n");
    fflush(stdout);
    _Exit(1);
}

static int listenLoopback(int& port) {
    int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    socklen_t len = sizeof(addr);
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || ::listen(s, 8) != 0 ||
        ::getsockname(s, (sockaddr*)&addr, &len) != 0) {
        ::close(s);
        return -1;
    }
    port = ntohs(addr.sin_port);
    return s;
}

static int acceptWithin(int listener, int ms) {
    pollfd pfd{listener, POLLIN, 0};
    if (::poll(&pfd, 1, ms) <= 0) return -1;
    return ::accept(listener, nullptr, nullptr);
}

// Close with SO_LINGER {1, 0}: the peer sends RST instead of FIN
static void resetPeer(int s) {
    linger lg;
    lg.l_onoff = 1;
    lg.l_linger = 0;
    ::setsockopt(s, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    ::close(s);
}

static void scenario() {
    int port = 0;
    int listener = listenLoopback(port);
    check("loopback listener is up", listener >= 0);
    if (listener < 0) bail();

    const vector<char> chunk(64 * 1024, 'x');

    // --- part 1: sends straight into a reset connection -----------------------
    {
        TcpClient client;
        client.setUseThread(false);
        check("part 1: connect() starts", client.connect("127.0.0.1", port));
        int peer = acceptWithin(listener, 2000);
        check("part 1: peer accepted the connection", peer >= 0);
        if (peer < 0) bail();

        // No app loop runs here, so drive the non-blocking connect by hand.
        // After this nothing calls processNetwork() again, so connected_ stays
        // true and every send below goes to the socket.
        check("part 1: client is connected",
              waitFor(2000, [&] { client.processNetwork(); return client.isConnected(); }));
        if (g_fail) bail();

        resetPeer(peer);
        this_thread::sleep_for(chrono::milliseconds(200));   // let the RST land

        int failed = 0;
        for (int i = 0; i < 1000; ++i) {
            if (!client.send(chunk.data(), chunk.size())) ++failed;   // pre-fix: signal 13
        }
        printf("  (part 1: %d of 1000 sends failed)\n", failed);
        check("part 1: the process survives sends to a reset peer", true);
        check("part 1: send() to a reset peer returns false", failed > 0);
    }

    // --- part 2: the threaded race --------------------------------------------
    {
        TcpClient client;
        atomic<bool> disconnected{false};
        EventListener sub = client.onDisconnect.listen([&](TcpDisconnectEventArgs&) {
            disconnected = true;
        });
        check("part 2: connect()", client.connect("127.0.0.1", port));
        int peer = acceptWithin(listener, 2000);
        check("part 2: peer accepted the connection", peer >= 0);
        check("part 2: client is connected", waitFor(2000, [&] { return client.isConnected(); }));
        if (g_fail) bail();

        resetPeer(peer);   // and send at once, racing the receive thread

        int failed = 0;
        for (int i = 0; i < 1000; ++i) {
            if (!client.send(chunk.data(), chunk.size())) ++failed;
        }
        printf("  (part 2: %d of 1000 sends failed)\n", failed);
        check("part 2: the process survives sends to a reset peer", true);
        check("part 2: send() returns false or onDisconnect fires",
              failed > 0 || waitFor(2000, [&] { return disconnected.load(); }));
    }

    ::close(listener);
}

} // namespace

TC_CORE_TEST_MAIN() {
    // The invariant is that TcpClient does not need SIGPIPE ignored. Make sure
    // nothing in the process ignores it for us, or this test proves nothing.
    std::signal(SIGPIPE, SIG_DFL);
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

#endif
