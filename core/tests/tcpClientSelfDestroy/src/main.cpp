// =============================================================================
// core/tests/tcpClientSelfDestroy — a TcpClient destroyed by a listener on
// its own receive thread (#262).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// An owner that replaces its client from inside one of the client's events
// (as WebSocketClient::connect() does when the app reconnects from onClose)
// destroys the client on that client's own receive thread. The receive
// thread must not read the client once the notification returns.
//
// Guards the invariants:
//   - An inline onDisconnect listener (the peer closed the connection)
//     replaces the unique_ptr<TcpClient> that holds the client, 50 times.
//   - The same when an error ends the connection (the peer resets it).
//   - The same from an inline onReceive listener, 50 times.
//   Each round connects the new client, so a client that was not left in a
//   usable state shows up as a failed connect.
//   - The receive threads of the destroyed clients end (counted on Linux).
//
// A read of a destroyed client usually goes unnoticed in a plain build, so
// this test is most useful built with AddressSanitizer
// (-DCMAKE_CXX_FLAGS=-fsanitize=address) or run on the Windows debug heap.
// The scenario runs on a worker with a deadline, so a hang reports FAIL
// instead of eating the CI job timeout.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

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

#ifdef __linux__
    #include <dirent.h>
#endif

using namespace std;
using namespace tc;

static const rawsocket_t kNoSocket = static_cast<rawsocket_t>(-1);
static atomic<int> g_fail{0};

static void check(const string& name, bool ok) {
    printf("%-60s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    fflush(stdout);   // flush per line so CI logs survive a later abort
    if (!ok) ++g_fail;
}

// Run fn on a worker and report failure if it does not finish in time
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

[[noreturn]] static void bail() {
    printf("\nFAILED\n");
    fflush(stdout);
    _Exit(1);
}

// A listening socket on 127.0.0.1 with a port the OS picks
static rawsocket_t listenLoopback(int& port) {
    rawsocket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNoSocket) return kNoSocket;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    socklen_t len = sizeof(addr);
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        ::getsockname(s, (sockaddr*)&addr, &len) != 0 ||
        ::listen(s, 8) != 0) {
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

// Close so that the client's recv() fails with an error (RST), not EOF
static void resetConnection(rawsocket_t s) {
    linger lg;
    lg.l_onoff = 1;
    lg.l_linger = 0;
    ::setsockopt(s, SOL_SOCKET, SO_LINGER, (const char*)&lg, sizeof(lg));
    TC_CLOSE(s);
}

#ifdef __linux__
// Entries in a /proc directory: threads of this process
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

// How the peer ends each round, and which event the owner replaces its
// client from
enum class Mode { RemoteClose, RemoteReset, Receive };

// Holds the client in a unique_ptr and replaces it from inside the client's
// own event, on the client's receive thread.
struct Owner {
    unique_ptr<TcpClient> client;
    EventListener sub;
    atomic<int> replaced{0};
    Mode mode;

    explicit Owner(Mode m) : mode(m) { make(); }

    void make() {
        client = make_unique<TcpClient>();   // destroys the previous client
        if (mode == Mode::Receive) {
            sub = client->onReceive.listen([this](TcpReceiveEventArgs&) { replace(); });
        } else {
            sub = client->onDisconnect.listen([this](TcpDisconnectEventArgs& e) {
                if (e.reason == "Disconnected by client") return;
                replace();
            });
        }
    }
    void replace() {
        make();
        ++replaced;
    }
};

static void runMode(const string& name, Mode mode, rawsocket_t listener, int port) {
    const int rounds = 50;
    Owner owner(mode);
    bool connected = true, accepted = true, replacedEachRound = true;
    for (int i = 0; i < rounds; ++i) {
        // The owner's client changes only from its listener, and only after
        // the peer below has acted; replaced says when that happened.
        const int before = owner.replaced.load();
        if (!owner.client->connect("127.0.0.1", port)) { connected = false; break; }
        rawsocket_t peer = acceptWithin(listener, 3000);
        if (peer == kNoSocket) { accepted = false; break; }
        if (mode == Mode::RemoteClose) {
            TC_CLOSE(peer);
        } else if (mode == Mode::RemoteReset) {
            resetConnection(peer);
        } else {
            ::send(peer, "x", 1, 0);
        }
        if (!waitFor(3000, [&] { return owner.replaced.load() > before; })) {
            replacedEachRound = false;
            if (mode == Mode::Receive) TC_CLOSE(peer);
            break;
        }
        if (mode == Mode::Receive) TC_CLOSE(peer);
    }
    check(name + ": connect() succeeds every round", connected);
    check(name + ": the peer accepts every round", accepted);
    check(name + ": the listener replaced the client 50 times",
          replacedEachRound && owner.replaced.load() == rounds);
}

static void scenario() {
    // Constructed first: it also starts Winsock
    TcpClient winsock;

    int port = 0;
    rawsocket_t listener = listenLoopback(port);
    check("loopback listener is up", listener != kNoSocket);
    if (g_fail) bail();

#ifdef __linux__
    const int threadsBaseline = countEntries("/proc/self/task");
#endif

    runMode("onDisconnect (remote close)", Mode::RemoteClose, listener, port);
    runMode("onDisconnect (connection error)", Mode::RemoteReset, listener, port);
    runMode("onReceive", Mode::Receive, listener, port);

#ifdef __linux__
    int threadsAfter = -1;
    waitFor(2000, [&] {
        threadsAfter = countEntries("/proc/self/task");
        return threadsAfter <= threadsBaseline;
    });
    printf("  (threads: %d before, %d after)\n", threadsBaseline, threadsAfter);
    check("the destroyed clients' receive threads ended", threadsAfter <= threadsBaseline);
#else
    printf("%-60s %s\n", "the destroyed clients' receive threads ended", "SKIP (counted on Linux)");
#endif

    TC_CLOSE(listener);
}

int main() {
    if (!completesWithin(60000, scenario)) {
        check("scenario finished within 60 s", false);
        bail();
    }
    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    fflush(stdout);
    return g_fail ? 1 : 0;
}
