// =============================================================================
// core/tests/tcpServerClients — behavioral regression test for TcpServer's
// client bookkeeping.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards:
//   - A client that leaves (closes, or resets the connection) has its receive
//     thread reclaimed while the server keeps running. Those threads used to
//     stay unjoined until stop(), one per client that ever left.
//   - start(port, N) caps the connected clients at N: a connection beyond that
//     is closed at once, a burst of them logs one warning, and a slot that
//     frees up can be taken again.
//   - start(port) with no limit accepts any number of clients.
//   - Linux only, each in a forked child so a failure cannot take the rest of
//     the run with it: accept() errors (here: out of descriptors) back off
//     instead of spinning and are logged once per burst, and a thread that
//     cannot be started for a new client closes that connection instead of
//     ending the process.
//
// Ports: TcpServer::getPort() returns the port that was passed to start(), so
// start(0) cannot report where it landed. Each server therefore takes a port
// the OS has just handed out to a throwaway socket, instead of a fixed one.
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
    #include <sys/time.h>
    #include <errno.h>
    #define TC_CLOSE ::close
    using rawsocket_t = int;
#endif

#ifdef __linux__
    #include <dirent.h>
    #include <fcntl.h>
    #include <signal.h>
    #include <sys/resource.h>
    #include <sys/wait.h>
    #include <time.h>
#endif

using namespace std;
using namespace tc;

static const rawsocket_t kBadSocket = static_cast<rawsocket_t>(-1);

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);   // flush per line so CI logs survive a later hang
    if (!ok) ++g_fail;
}

static void skip(const char* name, const char* why) {
    printf("%-60s SKIP (%s)\n", name, why);
    fflush(stdout);
}

template <typename Pred>
static bool waitUntil(int ms, Pred pred) {
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        this_thread::sleep_for(chrono::milliseconds(5));
    }
    return pred();
}

// A port the OS just handed out, released again for the server to bind
static int freePort() {
    rawsocket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBadSocket) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    int port = 0;
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
        socklen_t len = sizeof(addr);
        if (::getsockname(s, (sockaddr*)&addr, &len) == 0) port = ntohs(addr.sin_port);
    }
    TC_CLOSE(s);
    return port;
}

// Start a server on a fresh port, retrying if another process took it meanwhile
static int startOnFreePort(TcpServer& server, int maxClients) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        const int port = freePort();
        if (port == 0) continue;
        const bool ok = maxClients < 0 ? server.start(port) : server.start(port, maxClients);
        if (ok) return port;
    }
    return 0;
}

static rawsocket_t connectTo(int port) {
    rawsocket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBadSocket) return kBadSocket;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        TC_CLOSE(s);
        return kBadSocket;
    }
    return s;
}

// Close with a reset instead of a FIN, so the server sees a connection error
static void resetAndClose(rawsocket_t s) {
    linger lg;
    lg.l_onoff = 1;
    lg.l_linger = 0;
    ::setsockopt(s, SOL_SOCKET, SO_LINGER, (const char*)&lg, sizeof(lg));
    TC_CLOSE(s);
}

// True if the server closed this connection within `ms`
static bool closedByServer(rawsocket_t s, int ms) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    char c;
    const int n = static_cast<int>(::recv(s, &c, 1, 0));
    if (n == 0) return true;   // orderly close
    if (n > 0) return false;
#ifdef _WIN32
    const int err = WSAGetLastError();
    return err != WSAETIMEDOUT && err != WSAEWOULDBLOCK;
#else
    return errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR;
#endif
}

#ifdef __linux__
// Live threads in this process
static int taskCount() {
    int n = 0;
    if (DIR* d = opendir("/proc/self/task")) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] != '.') ++n;
        }
        closedir(d);
    }
    return n;
}

// Mapped address space, in KiB. A thread that has returned but was never
// joined no longer shows up in /proc/self/task — the kernel reaps the task
// itself — yet its stack stays mapped until the join, so this is what reveals
// threads that were never reclaimed.
static long vmSizeKb() {
    long kb = -1;
    if (FILE* f = fopen("/proc/self/status", "r")) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "VmSize:", 7) == 0) {
                kb = strtol(line + 7, nullptr, 10);
                break;
            }
        }
        fclose(f);
    }
    return kb;
}

static double processCpuSeconds() {
    timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Run fn in a forked child and fold its result into this process. The child
// prints its own check lines; a crash or hang there is one FAIL here.
template <typename F>
static void inChild(const char* name, F fn) {
    fflush(stdout);
    fflush(stderr);
    const pid_t pid = fork();
    if (pid < 0) {
        check(name, false);
        return;
    }
    if (pid == 0) {
        alarm(60);   // a hang ends as SIGALRM rather than holding up the run
        g_fail = 0;
        fn();
        fflush(stdout);
        fflush(stderr);
        _exit(g_fail ? 1 : 0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) {
        printf("  (child ended by signal %d)\n", WTERMSIG(status));
        check(name, false);
    } else {
        check(name, WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}
#endif

// Counts the warnings the server logs, by what they say
struct WarningCounter {
    atomic<int> full{0};
    atomic<int> acceptFailed{0};
    atomic<int> threadFailed{0};
    EventListener sub;

    WarningCounter() {
        sub = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.level != LogLevel::Warning) return;
            if (e.message.find(" is full ") != string::npos) ++full;
            if (e.message.find("Failed to accept a connection") != string::npos) ++acceptFailed;
            if (e.message.find("Could not start a thread") != string::npos) ++threadFailed;
        });
    }
};

// -----------------------------------------------------------------------------
// Receive threads of clients that leave are reclaimed while the server runs
// -----------------------------------------------------------------------------
static void testReclaim() {
    TcpServer server;
    const int port = startOnFreePort(server, -1);
    check("reclaim: server started", port != 0);
    if (!port) return;

    // One round: a batch of clients connect, then half close and half reset,
    // so both ways a receive thread removes its own client are covered.
    auto churn = [&](int batches, int perBatch) {
        for (int b = 0; b < batches; ++b) {
            vector<rawsocket_t> peers;
            for (int i = 0; i < perBatch; ++i) {
                rawsocket_t s = connectTo(port);
                if (s != kBadSocket) peers.push_back(s);
            }
            if (!waitUntil(3000, [&] { return server.getClientCount() == (int)peers.size(); }))
                return false;
            for (size_t i = 0; i < peers.size(); ++i) {
                if (i % 2) resetAndClose(peers[i]);
                else TC_CLOSE(peers[i]);
            }
            if (!waitUntil(3000, [&] { return server.getClientCount() == 0; })) return false;
        }
        return true;
    };

    // Warm up first, so allocator and thread-stack caches are already at their
    // steady size when the baseline is taken
    check("reclaim: warm-up clients come and go", churn(2, 10));

#ifdef __linux__
    this_thread::sleep_for(chrono::milliseconds(300));   // let the reaper catch up
    const int tasksBefore = taskCount();
    const long vmBefore = vmSizeKb();
#endif

    const int kBatches = 20, kPerBatch = 10;   // 200 clients in all
    check("reclaim: 200 clients connect and leave", churn(kBatches, kPerBatch));
    check("reclaim: client list is empty afterwards", server.getClientCount() == 0 &&
                                                        server.getClientIds().empty());

#ifdef __linux__
    // The accept loop reaps on its next slice (100 ms)
    const bool tasksBack = waitUntil(3000, [&] { return taskCount() <= tasksBefore; });
    const int tasksAfter = taskCount();
    this_thread::sleep_for(chrono::milliseconds(300));
    const long vmAfter = vmSizeKb();
    const long growthMb = (vmAfter - vmBefore) / 1024;
    printf("  (threads %d -> %d, address space %+ld MB)\n", tasksBefore, tasksAfter, growthMb);

    check("reclaim: thread count returns to baseline", tasksBack);
    // 200 unjoined threads keep 200 stacks mapped: 8 MB each by default, so
    // well over a gigabyte. Joined ones go back to a small shared cache.
    check("reclaim: finished threads are joined (no stacks left mapped)",
          vmBefore > 0 && vmAfter > 0 && growthMb < 256);
#else
    skip("reclaim: thread count returns to baseline", "Linux only");
#endif

    server.stop();
}

// -----------------------------------------------------------------------------
// start(port, N) caps connected clients at N
// -----------------------------------------------------------------------------
static void testLimit() {
    WarningCounter warnings;
    TcpServer server;
    const int kLimit = 4;
    const int port = startOnFreePort(server, kLimit);
    check("limit: server started with maxClients = 4", port != 0);
    if (!port) return;

    vector<rawsocket_t> members;
    for (int i = 0; i < kLimit; ++i) members.push_back(connectTo(port));
    check("limit: the first 4 connect",
          waitUntil(3000, [&] { return server.getClientCount() == kLimit; }));

    // A burst beyond the limit
    const int kExtra = 5;
    vector<rawsocket_t> extras;
    for (int i = 0; i < kExtra; ++i) extras.push_back(connectTo(port));
    int closed = 0;
    for (rawsocket_t s : extras) {
        if (s != kBadSocket && closedByServer(s, 3000)) ++closed;
    }
    check("limit: every connection over the limit is closed", closed == kExtra);
    this_thread::sleep_for(chrono::milliseconds(200));
    check("limit: still exactly 4 clients", server.getClientCount() == kLimit);
    check("limit: the burst logs exactly one warning", warnings.full.load() == 1);
    printf("  (%d \"is full\" warning(s))\n", warnings.full.load());
    for (rawsocket_t s : extras) if (s != kBadSocket) TC_CLOSE(s);

    // Members are not affected
    int membersOpen = 0;
    for (rawsocket_t s : members) {
        if (s != kBadSocket && !closedByServer(s, 50)) ++membersOpen;
    }
    check("limit: the 4 admitted clients stay connected", membersOpen == kLimit);

    // A slot that frees up can be taken again
    TC_CLOSE(members.back());
    members.pop_back();
    check("limit: a leaving client frees its slot",
          waitUntil(3000, [&] { return server.getClientCount() == kLimit - 1; }));
    rawsocket_t late = connectTo(port);
    check("limit: a new client takes the freed slot",
          waitUntil(3000, [&] { return server.getClientCount() == kLimit; }) &&
          late != kBadSocket && !closedByServer(late, 300));
    if (late != kBadSocket) TC_CLOSE(late);

    for (rawsocket_t s : members) if (s != kBadSocket) TC_CLOSE(s);
    server.stop();
}

// -----------------------------------------------------------------------------
// start(port) accepts any number of clients
// -----------------------------------------------------------------------------
static void testDefaultUnlimited() {
    WarningCounter warnings;
    TcpServer server;
    const int port = startOnFreePort(server, -1);
    check("default: server started without a limit", port != 0);
    if (!port) return;

    const int kClients = 16;
    vector<rawsocket_t> peers;
    for (int i = 0; i < kClients; ++i) peers.push_back(connectTo(port));
    check("default: 16 clients are all accepted",
          waitUntil(3000, [&] { return server.getClientCount() == kClients; }));

    int open = 0;
    for (rawsocket_t s : peers) {
        if (s != kBadSocket && !closedByServer(s, 50)) ++open;
    }
    check("default: none of them is closed", open == kClients);
    check("default: nothing is reported as full", warnings.full.load() == 0);

    for (rawsocket_t s : peers) if (s != kBadSocket) TC_CLOSE(s);
    server.stop();
}

#ifdef __linux__
// -----------------------------------------------------------------------------
// accept() errors back off (runs in a forked child: it exhausts descriptors)
// -----------------------------------------------------------------------------
static void acceptBackoffChild() {
    WarningCounter warnings;
    atomic<int> errors{0};
    TcpServer server;
    EventListener onErr = server.onError.listen([&](TcpServerErrorEventArgs&) { ++errors; });
    const int port = startOnFreePort(server, -1);
    check("accept errors: server started", port != 0);
    if (!port) return;

    // A low descriptor limit, then fill it: with none left, every accept()
    // fails and the pending connection stays queued, so the listening socket
    // stays readable for as long as the condition lasts.
    rlimit lim;
    getrlimit(RLIMIT_NOFILE, &lim);
    rlimit low = lim;
    low.rlim_cur = 128;
    setrlimit(RLIMIT_NOFILE, &low);

    vector<int> fillers;
    for (;;) {
        int fd = ::open("/dev/null", O_RDONLY);
        if (fd < 0) break;
        fillers.push_back(fd);
    }
    // One descriptor back for the client's own socket
    ::close(fillers.back());
    fillers.pop_back();
    rawsocket_t client = connectTo(port);
    check("accept errors: client connects into the backlog", client != kBadSocket);

    const double cpu0 = processCpuSeconds();
    this_thread::sleep_for(chrono::milliseconds(1000));
    const double cpuUsed = processCpuSeconds() - cpu0;
    printf("  (%.3f s of CPU in 1 s of failing accepts)\n", cpuUsed);

    check("accept errors: the accept loop does not spin", cpuUsed < 0.25);
    check("accept errors: nothing was accepted", server.getClientCount() == 0);
    check("accept errors: logged exactly once for the burst", warnings.acceptFailed.load() == 1);
    check("accept errors: reported through onError", errors.load() >= 1);

    // Recovery: the queued connection is taken once descriptors are free again
    for (int fd : fillers) ::close(fd);
    setrlimit(RLIMIT_NOFILE, &lim);
    check("accept errors: the queued client is accepted afterwards",
          waitUntil(3000, [&] { return server.getClientCount() == 1; }));

    if (client != kBadSocket) TC_CLOSE(client);
    server.stop();
}

// -----------------------------------------------------------------------------
// A thread that cannot be started closes that connection, not the process
// (runs in a forked child: it takes away the right to create threads)
// -----------------------------------------------------------------------------
static void threadStartFailureChild() {
    WarningCounter warnings;
    atomic<int> errors{0};
    TcpServer server;
    EventListener onErr = server.onError.listen([&](TcpServerErrorEventArgs&) { ++errors; });
    const int port = startOnFreePort(server, -1);
    check("thread start: server started", port != 0);
    if (!port) return;

    // RLIMIT_NPROC counts every process and thread of this user, so a limit of
    // 1 makes the next thread creation fail
    rlimit lim;
    getrlimit(RLIMIT_NPROC, &lim);
    rlimit low = lim;
    low.rlim_cur = 1;
    setrlimit(RLIMIT_NPROC, &low);

    rawsocket_t client = connectTo(port);
    check("thread start: client connects", client != kBadSocket);
    check("thread start: the connection is closed",
          client != kBadSocket && closedByServer(client, 3000));
    check("thread start: no client is registered", server.getClientCount() == 0);
    check("thread start: reported through onError",
          waitUntil(1000, [&] { return errors.load() >= 1; }));
    check("thread start: logged", warnings.threadFailed.load() >= 1);
    if (client != kBadSocket) TC_CLOSE(client);

    // Recovery
    setrlimit(RLIMIT_NPROC, &lim);
    rawsocket_t again = connectTo(port);
    check("thread start: a client is accepted once threads can start",
          waitUntil(3000, [&] { return server.getClientCount() == 1; }));
    if (again != kBadSocket) TC_CLOSE(again);

    server.stop();
}
#endif

int main() {
#ifdef __linux__
    // Forked first, while this process has no threads of its own yet
    inChild("accept errors back off (child)", acceptBackoffChild);
    if (geteuid() == 0) {
        skip("a failed thread start closes the connection (child)",
             "root is not bound by RLIMIT_NPROC");
    } else {
        inChild("a failed thread start closes the connection (child)", threadStartFailureChild);
    }
#else
    skip("accept errors back off", "Linux only (fork + RLIMIT_NOFILE)");
    skip("a failed thread start closes the connection", "Linux only (fork + RLIMIT_NPROC)");
#endif

    testReclaim();
    testLimit();
    testDefaultUnlimited();

    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
