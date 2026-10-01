// =============================================================================
// core/tests/tcpServerClients — behavioral regression test for TcpServer's
// client bookkeeping.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards:
//   - A client that leaves (closes, or resets the connection) has its receive
//     and writer threads reclaimed while the server keeps running. Those
//     threads used to stay unjoined until stop(), one per client that ever
//     left.
//   - start(port, N) caps the connected clients at N: a connection beyond that
//     is closed at once, a burst of them logs one warning, and a slot that
//     frees up can be taken again.
//   - start(port) with no limit accepts any number of clients.
//   - Listeners may tear down from the thread they run on: disconnectClient()
//     of its own client in onReceive (the server's destruction then waits for
//     that thread instead of leaving it behind), stop() in onReceive, and
//     stop() in onClientConnect, which runs on the accept thread and still
//     closes the listening socket before it returns.
//     The accept thread disconnects the clients once that listener returns.
//   - A listener's teardown waits for none of the server's threads, and the
//     server's destruction waits for all of them: stop() from onError after a
//     send timed out mid-payload while the receive thread removes that client
//     (a later start() works; SKIP where the send is not cut mid-payload, as
//     Winsock may take the whole payload in one send()); disconnectClient() of its own client, or
//     stop(), from onSendComplete with the server destroyed while the
//     listener still runs; two onReceive listeners disconnecting each other's
//     client; two onReceive, or two onSendComplete, listeners each calling
//     disconnectAllClients() (the server keeps running); disconnectAllClients()
//     on another thread while a client connects, which leaves that client
//     connected.
//   - stop() called on several threads at once returns everywhere: from
//     onClientConnect (accept thread) together with one from another client's
//     onReceive or onSendComplete, in either order; from two clients'
//     onReceive, or two onSendComplete; from a plain thread together with
//     onSendComplete (the plain one waits for that listener to finish), or
//     with onClientConnect once the plain one has taken the accept thread
//     (the accept-taken hook orders them; the listening socket is still
//     closed before the listener's stop() returns); and from two plain
//     threads. The stop hook orders the plain thread and the
//     writer. Every client ends up disconnected. start()
//     while another thread's stop() still waits for the accept thread waits
//     for it too, and so does start() while that stop() has joined the accept
//     thread but not yet cleaned up the clients (held by the stop hook): a
//     client of the restarted server stays connected. start() from onReceive
//     is refused. A watchdog turns a hang into a FAIL line.
//   - Linux only, each in a forked child so a failure cannot take the rest of
//     the run with it: accept() errors (here: out of descriptors) back off
//     instead of spinning, are logged once per burst and reported again after
//     the throttle interval if they persist; a thread that cannot be started
//     for a new client closes that connection instead of ending the process,
//     both for the writer (RLIMIT_NPROC) and for the receive thread (a
//     pthread_create wrapper in this binary that fails one chosen call, where
//     the accept thread must not wait for that client's writer); and
//     start() whose accept thread cannot be started returns false, reports
//     it and leaves nothing listening.
//
// Ports: TcpServer::getPort() returns the port that was passed to start(), so
// start(0) cannot report where it landed. Each server therefore takes a port
// the OS has just handed out to a throwaway socket, instead of a fixed one.
// =============================================================================

#include <TrussC.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    #include <errno.h>
    #define TC_CLOSE ::close
    using rawsocket_t = int;
#endif

#ifdef __linux__
    #include <dirent.h>
    #include <dlfcn.h>
    #include <fcntl.h>
    #include <pthread.h>
    #include <signal.h>
    #include <sys/resource.h>
    #include <sys/wait.h>
    #include <time.h>
#endif

using namespace std;
using namespace tc;

#ifdef __linux__
// How many thread creations to let through before the next one fails with
// EAGAIN, as if the process were out of resources; -1 = never. Disarms itself
// after that one failure.
static atomic<int> g_threadStartsBeforeFailure{-1};

// This definition takes precedence over libc's for every thread the process
// creates, std::thread included, and forwards to the real one.
extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                              void* (*start)(void*), void* arg) {
    using Fn = int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
    static const Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "pthread_create"));
    int left = g_threadStartsBeforeFailure.load();
    while (left >= 0) {
        const int next = left == 0 ? -1 : left - 1;
        if (g_threadStartsBeforeFailure.compare_exchange_weak(left, next)) {
            if (left == 0) return EAGAIN;
            break;
        }
    }
    return real(thread, attr, start, arg);
}
#endif

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

// Ends the process with a FAIL line unless it is destroyed within `ms`. A
// deadlock would otherwise hold up the whole run instead of failing it.
class Watchdog {
public:
    Watchdog(const char* name, int ms) {
        thread_ = thread([this, name, ms] {
            unique_lock<mutex> lock(mutex_);
            if (!cv_.wait_for(lock, chrono::milliseconds(ms), [this] { return done_; })) {
                printf("%-60s FAIL (still running after %d ms)\n", name, ms);
                fflush(stdout);
                _Exit(1);
            }
        });
    }
    ~Watchdog() {
        {
            lock_guard<mutex> lock(mutex_);
            done_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

private:
    mutex mutex_;
    condition_variable cv_;
    bool done_ = false;
    thread thread_;
};

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

// True if the server closed this connection within `ms`, whatever it sent
// before that
static bool closedAfterData(rawsocket_t s, int ms) {
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    char buf[65536];
    while (chrono::steady_clock::now() < deadline) {
        const int left = max(1, static_cast<int>(chrono::duration_cast<chrono::milliseconds>(
                                                     deadline - chrono::steady_clock::now()).count()));
#ifdef _WIN32
        DWORD tv = static_cast<DWORD>(left);
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = left / 1000;
        tv.tv_usec = (left % 1000) * 1000;
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        const int n = static_cast<int>(::recv(s, buf, sizeof(buf), 0));
        if (n > 0) continue;
        if (n == 0) return true;
#ifdef _WIN32
        const int err = WSAGetLastError();
        return err != WSAETIMEDOUT && err != WSAEWOULDBLOCK;
#else
        if (errno == EINTR) continue;
        return errno != EAGAIN && errno != EWOULDBLOCK;
#endif
    }
    return false;
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

// Inode of the socket listening on this port (IPv4), 0 if there is none
static unsigned long listeningInode(int port) {
    unsigned long inode = 0;
    if (FILE* f = fopen("/proc/net/tcp", "r")) {
        char line[512];
        bool header = true;
        while (fgets(line, sizeof(line), f)) {
            if (header) {
                header = false;
                continue;
            }
            unsigned localPort = 0, state = 0;
            unsigned long ino = 0;
            // sl local rem st tx:rx tr:when retrnsmt uid timeout inode
            if (sscanf(line, " %*d: %*x:%x %*x:%*x %x %*x:%*x %*x:%*x %*x %*u %*u %lu",
                       &localPort, &state, &ino) == 3 &&
                static_cast<int>(localPort) == port && state == 0x0A) {   // LISTEN
                inode = ino;
                break;
            }
        }
        fclose(f);
    }
    return inode;
}

// Whether this process still has a descriptor open on that socket. A socket
// that was shut down but not closed has left /proc/net/tcp, yet still holds
// its port and shows up here.
static bool holdsSocket(unsigned long inode) {
    bool held = false;
    const string want = "socket:[" + to_string(inode) + "]";
    if (DIR* d = opendir("/proc/self/fd")) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            char target[64];
            const string link = string("/proc/self/fd/") + e->d_name;
            const ssize_t n = readlink(link.c_str(), target, sizeof(target) - 1);
            if (n <= 0) continue;
            target[n] = '\0';
            if (want == target) {
                held = true;
                break;
            }
        }
        closedir(d);
    }
    return held;
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

    // The same without a later connection. A server that reaped only when the
    // next client arrived would pass the check above (it holds just the last
    // batch) and would start every measurement here with a batch held, so
    // first one probe client, whose accept is where such a server would reap.
    const int kIdleBatch = 40;
    size_t stackBytes = 0;
    pthread_attr_t attr;
    if (pthread_getattr_default_np(&attr) == 0) {
        pthread_attr_getstacksize(&attr, &stackBytes);
        pthread_attr_destroy(&attr);
    }
    const long stackMb = static_cast<long>(stackBytes / (1024 * 1024));
    if (stackMb >= 4) {
        // Warm up at this batch size too: more threads at once than before
        // can make the allocator map further arenas, which stay mapped
        const bool warm = churn(2, kIdleBatch);
        const bool probed = warm && churn(1, 1);
        this_thread::sleep_for(chrono::milliseconds(300));
        const long vmIdle = vmSizeKb();
        const bool left = churn(1, kIdleBatch);
        // No further connection: only the accept loop's own slices can reap
        this_thread::sleep_for(chrono::milliseconds(500));
        const long heldMb = (vmSizeKb() - vmIdle) / 1024;
        printf("  (%d clients left an idle server: address space %+ld MB, %ld MB per stack)\n",
               kIdleBatch, heldMb, stackMb);
        // Held, their receive stacks alone would be kIdleBatch * stackMb
        check("reclaim: an idle server joins them without a new connection",
              probed && left && vmIdle > 0 && heldMb < kIdleBatch * stackMb / 2);
    } else {
        skip("reclaim: an idle server joins them without a new connection",
             "default thread stack under 4 MB");
    }
#else
    skip("reclaim: thread count returns to baseline", "Linux only");
    skip("reclaim: an idle server joins them without a new connection", "Linux only");
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

// -----------------------------------------------------------------------------
// Listeners that tear down from the thread they run on
// -----------------------------------------------------------------------------
static void testListenerTeardown() {
    // disconnectClient() of its own client in onReceive, then the server is
    // destroyed while that listener is still running. The receive thread still
    // touches the server on its way out, so the destruction has to wait for it.
    {
        auto server = make_unique<TcpServer>();
        TcpServer* srv = server.get();
        atomic<bool> listenerDone{false};
        EventListener sub = server->onReceive.listen([&, srv](TcpServerReceiveEventArgs& e) {
            srv->disconnectClient(e.clientId);
            this_thread::sleep_for(chrono::milliseconds(300));
            listenerDone = true;
        });
        const int port = startOnFreePort(*server, -1);
        check("teardown: server started", port != 0);
        rawsocket_t c = port ? connectTo(port) : kBadSocket;
        const bool joined = c != kBadSocket &&
                            waitUntil(3000, [&] { return srv->getClientCount() == 1; });
        if (joined) ::send(c, "x", 1, 0);
        check("teardown: onReceive disconnects its own client",
              joined && waitUntil(3000, [&] { return srv->getClientCount() == 0; }));
        server.reset();
        check("teardown: destroying the server waits for that receive thread",
              listenerDone.load());
        if (!listenerDone) this_thread::sleep_for(chrono::milliseconds(500));
        if (c != kBadSocket) TC_CLOSE(c);
    }

    // stop() in onReceive: every client is disconnected before it returns,
    // though it joins none of the server's threads
    {
        TcpServer server;
        atomic<bool> stopped{false};
        EventListener sub = server.onReceive.listen([&](TcpServerReceiveEventArgs&) {
            server.stop();
            stopped = true;
        });
        const int port = startOnFreePort(server, -1);
        rawsocket_t other = port ? connectTo(port) : kBadSocket;
        rawsocket_t c = port ? connectTo(port) : kBadSocket;
        waitUntil(3000, [&] { return server.getClientCount() == 2; });
        if (c != kBadSocket) ::send(c, "x", 1, 0);
        check("teardown: stop() from onReceive returns",
              waitUntil(3000, [&] { return stopped.load(); }) && !server.isRunning());
        check("teardown: and it disconnected everyone", server.getClientCount() == 0);
        const int again = startOnFreePort(server, -1);
        check("teardown: the server starts again afterwards", again != 0);
        server.stop();
        if (c != kBadSocket) TC_CLOSE(c);
        if (other != kBadSocket) TC_CLOSE(other);
    }

    // stop() in onClientConnect, which fires on the accept thread: it cannot
    // join itself, but the listening socket is closed before it returns, so
    // the port is released at once rather than left listening (and taking
    // connections nobody accepts) until the thread is joined. start() from
    // there is refused rather than attempted.
    {
        TcpServer server;
        atomic<int> connects{0};
        atomic<int> oldPort{0};
        atomic<bool> restartRefused{false};
        atomic<bool> refusedAfterStop{false};
        atomic<bool> listenerDone{false};
#ifdef __linux__
        atomic<unsigned long> listenInode{0};
        atomic<bool> socketClosed{false};
#endif
        EventListener sub = server.onClientConnect.listen([&](TcpClientConnectEventArgs&) {
            if (connects++ != 0) return;
            server.stop();
            // Nothing may be listening on the old port any more
            rawsocket_t probe = connectTo(oldPort.load());
            refusedAfterStop = probe == kBadSocket;
            if (probe != kBadSocket) TC_CLOSE(probe);
#ifdef __linux__
            // On Linux shutdown() alone already refuses connections, so also
            // check that the descriptor itself is gone
            socketClosed = listenInode.load() != 0 && !holdsSocket(listenInode.load());
#endif
            restartRefused = !server.start(freePort());
            listenerDone = true;
        });
        const int port = startOnFreePort(server, -1);
        oldPort = port;
#ifdef __linux__
        listenInode = port ? listeningInode(port) : 0;
#endif
        rawsocket_t c = port ? connectTo(port) : kBadSocket;
        // A refused connect() can take a couple of seconds on Windows
        check("teardown: stop() from onClientConnect (accept thread) returns",
              waitUntil(3000, [&] { return connects.load() == 1; }) &&
              waitUntil(10000, [&] { return listenerDone.load(); }) &&
              !server.isRunning());
        check("teardown: the old port refuses connections once stop() returns",
              refusedAfterStop.load());
#ifdef __linux__
        check("teardown: the listening socket is closed once stop() returns",
              socketClosed.load());
#endif
        check("teardown: start() from the accept thread is refused", restartRefused.load());
        // stop() there returned before disconnecting anyone; the accept thread
        // does that once the listener returns, without a later stop()
        check("teardown: the client is disconnected once the listener returns",
              c != kBadSocket && closedByServer(c, 3000) &&
              waitUntil(3000, [&] { return server.getClientCount() == 0; }));
        const int again = startOnFreePort(server, -1);
        check("teardown: the server starts again from another thread", again != 0);
        rawsocket_t d = again ? connectTo(again) : kBadSocket;
        check("teardown: and accepts a client",
              waitUntil(3000, [&] { return server.getClientCount() == 1; }));
        server.stop();
        if (c != kBadSocket) TC_CLOSE(c);
        if (d != kBadSocket) TC_CLOSE(d);
    }
}

// stop() from onError after a send timed out mid-payload, while the peer does
// not read. The receive thread removes the dropped client, and the listener
// calls stop() only once it has: a removal that waited for the writer, with the
// writer's stop() waiting for that receive thread, would never return. A later
// start() must not wait for it either.
//
// The mid-payload timeout is a premise, not the invariant. Winsock may accept
// the whole payload in one send() and refuse a later one whole instead of
// cutting it part-way, so on a platform where the send is not cut the checks
// that depend on it report SKIP.
static void stopFromOnErrorAfterSendTimeout() {
    const char* const prefix = "teardown: stop() from onError after a send timeout";
    Watchdog dog(prefix, 20000);

    auto server = make_unique<TcpServer>();
    TcpServer* srv = server.get();
    srv->setSendTimeout(0.3f);
    atomic<bool> errorIn{false}, removedFirst{false}, stopped{false}, notCut{false};
    EventListener onErr = srv->onError.listen([&, srv](TcpServerErrorEventArgs& e) {
        if (e.message.find("mid-payload") == string::npos || errorIn.exchange(true)) return;
        removedFirst = waitUntil(3000, [&] { return srv->getClientCount() == 0; });
        srv->stop();
        stopped = true;
    });
    // The payload went out whole, or timed out having written nothing: either
    // way the stream was not cut
    EventListener onSent = srv->onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (e.error == SendError::None || e.error == SendError::Timeout) notCut = true;
    });

    const int port = startOnFreePort(*srv, -1);
    check((string(prefix) + ": server started").c_str(), port != 0);
    if (!port) return;

    rawsocket_t c = connectTo(port);
    const bool joined = c != kBadSocket &&
                        waitUntil(3000, [&] { return srv->getClientCount() == 1; });
    // Far more than the socket buffers on both ends hold; the peer never reads
    if (joined) srv->sendAsync(srv->getClientIds().front(), vector<char>(32 * 1024 * 1024, 'x'));

    const bool returned = joined && waitUntil(5000, [&] {
        return stopped.load() || (notCut.load() && !errorIn.load());
    }) && stopped.load();
    if (joined && !errorIn.load() && notCut.load()) {
        const char* const why = "the send was not cut mid-payload on this platform";
        skip((string(prefix) + ": the send times out and the client is removed first").c_str(), why);
        skip((string(prefix) + ": stop() returns").c_str(), why);
        skip((string(prefix) + ": the client is disconnected").c_str(), why);
        skip((string(prefix) + ": start() afterwards succeeds").c_str(), why);
        skip((string(prefix) + ": and the server accepts a client").c_str(), why);
        server.reset();
        check((string(prefix) + ": the server is destroyed afterwards").c_str(), true);
        if (c != kBadSocket) TC_CLOSE(c);
        return;
    }
    check((string(prefix) + ": the send times out and the client is removed first").c_str(),
          errorIn.load() && removedFirst.load());
    check((string(prefix) + ": stop() returns").c_str(), returned);
    check((string(prefix) + ": the client is disconnected").c_str(),
          c != kBadSocket && closedAfterData(c, 3000));

    const int again = returned ? startOnFreePort(*srv, -1) : 0;
    check((string(prefix) + ": start() afterwards succeeds").c_str(), again != 0);
    rawsocket_t d = again ? connectTo(again) : kBadSocket;
    check((string(prefix) + ": and the server accepts a client").c_str(),
          d != kBadSocket && waitUntil(3000, [&] { return srv->getClientCount() == 1; }));

    if (returned) {
        server.reset();
        check((string(prefix) + ": the server is destroyed afterwards").c_str(), true);
    } else {
        server.release();
    }
    if (c != kBadSocket) TC_CLOSE(c);
    if (d != kBadSocket) TC_CLOSE(d);
}

// An onSendComplete listener disconnects its own client (or, with `viaStop`,
// stops the server, so the accept loop is gone too) and keeps running; the
// server is destroyed from this thread meanwhile. The writer returns into the
// server once the listener is done, so the destruction has to wait for it.
// With `viaStop` it also guards destruction when no accept loop is left to
// reap that writer.
static void destroyWhileWriterListenerRuns(bool viaStop) {
    const string prefix = string("teardown: onSendComplete ") +
                          (viaStop ? "stops the server" : "disconnects its own client") +
                          ", then destruction";
    Watchdog dog(prefix.c_str(), 20000);

    auto server = make_unique<TcpServer>();
    TcpServer* srv = server.get();
    atomic<bool> listenerIn{false}, callReturned{false}, listenerDone{false};
    EventListener onSent = srv->onSendComplete.listen([&, srv](TcpSendCompleteEventArgs& e) {
        if (listenerIn.exchange(true)) return;
        if (viaStop) srv->stop();
        else srv->disconnectClient(e.clientId);
        callReturned = true;
        this_thread::sleep_for(chrono::milliseconds(300));
        listenerDone = true;
    });

    const int port = startOnFreePort(*srv, -1);
    check((prefix + ": server started").c_str(), port != 0);
    if (!port) return;

    rawsocket_t c = connectTo(port);
    const bool joined = c != kBadSocket &&
                        waitUntil(3000, [&] { return srv->getClientCount() == 1; });
    if (joined) srv->sendAsync(srv->getClientIds().front(), string("x"));
    // The client count reaches 0 inside the call, before it would wait for
    // anything, so what shows the call returned is the listener going on
    const bool gone = joined && waitUntil(3000, [&] { return callReturned.load(); }) &&
                      srv->getClientCount() == 0;
    if (viaStop) {
        // Past the accept loop's last slice: nothing reaps any more
        this_thread::sleep_for(chrono::milliseconds(200));
    }
    check((prefix + ": the listener's call returns").c_str(), gone);
    server.reset();
    check((prefix + ": destroying the server waits for that writer").c_str(),
          listenerDone.load());
    if (!listenerDone) this_thread::sleep_for(chrono::milliseconds(500));
    if (c != kBadSocket) TC_CLOSE(c);
}

// Two clients' onReceive listeners, each disconnecting the other's client once
// both are inside. Neither may wait for the other's receive thread: that one
// is waiting in turn.
static void crossDisconnectFromTwoListeners() {
    const char* const prefix = "teardown: two onReceive listeners disconnect each other";
    Watchdog dog(prefix, 20000);

    auto server = make_unique<TcpServer>();
    TcpServer* srv = server.get();
    atomic<int> inside{0}, returned{0};
    mutex seenMutex;
    vector<int> seen;   // clients whose listener has run, guarded by seenMutex
    EventListener onRecv = srv->onReceive.listen([&, srv](TcpServerReceiveEventArgs& e) {
        {
            lock_guard<mutex> lock(seenMutex);
            if (find(seen.begin(), seen.end(), e.clientId) != seen.end()) return;
            seen.push_back(e.clientId);
        }
        const vector<int> ids = srv->getClientIds();
        ++inside;
        waitUntil(3000, [&] { return inside.load() >= 2; });
        for (int id : ids) {
            if (id != e.clientId) srv->disconnectClient(id);
        }
        ++returned;
    });

    const int port = startOnFreePort(*srv, -1);
    check((string(prefix) + ": server started").c_str(), port != 0);
    if (!port) return;

    rawsocket_t a = connectTo(port);
    rawsocket_t b = connectTo(port);
    const bool joined = a != kBadSocket && b != kBadSocket &&
                        waitUntil(3000, [&] { return srv->getClientCount() == 2; });
    if (joined) {
        ::send(a, "x", 1, 0);
        ::send(b, "x", 1, 0);
    }
    const bool bothReturned = joined && waitUntil(5000, [&] { return returned.load() == 2; });
    check((string(prefix) + ": both calls return").c_str(), bothReturned);
    check((string(prefix) + ": both clients are disconnected").c_str(),
          joined && closedByServer(a, 3000) && closedByServer(b, 3000) &&
          waitUntil(3000, [&] { return srv->getClientCount() == 0; }));
    check((string(prefix) + ": the server keeps running").c_str(), srv->isRunning());

    if (bothReturned) {
        server.reset();
        check((string(prefix) + ": the server is destroyed afterwards").c_str(), true);
    } else {
        server.release();
    }
    if (a != kBadSocket) TC_CLOSE(a);
    if (b != kBadSocket) TC_CLOSE(b);
}

// Two clients' listeners (onReceive, or onSendComplete with `fromWriter`),
// each calling disconnectAllClients() once both are inside. On a server
// thread the call must join nothing: not its own thread, and not the other
// listener's, which is waiting in turn.
static void disconnectAllFromTwoListeners(bool fromWriter) {
    const string prefix = string("teardown: two ") +
                          (fromWriter ? "onSendComplete" : "onReceive") +
                          " listeners call disconnectAllClients()";
    Watchdog dog(prefix.c_str(), 20000);

    auto server = make_unique<TcpServer>();
    TcpServer* srv = server.get();
    atomic<int> inside{0}, returned{0};
    mutex seenMutex;
    vector<int> seen;   // clients whose listener has run, guarded by seenMutex
    auto listener = [&, srv](int clientId) {
        {
            lock_guard<mutex> lock(seenMutex);
            if (find(seen.begin(), seen.end(), clientId) != seen.end()) return;
            seen.push_back(clientId);
        }
        ++inside;
        waitUntil(3000, [&] { return inside.load() >= 2; });
        srv->disconnectAllClients();
        ++returned;
    };
    EventListener onRecv = srv->onReceive.listen([&](TcpServerReceiveEventArgs& e) {
        if (!fromWriter) listener(e.clientId);
    });
    EventListener onSent = srv->onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (fromWriter) listener(e.clientId);
    });

    const int port = startOnFreePort(*srv, -1);
    check((prefix + ": server started").c_str(), port != 0);
    if (!port) return;

    rawsocket_t a = connectTo(port);
    rawsocket_t b = connectTo(port);
    const bool joined = a != kBadSocket && b != kBadSocket &&
                        waitUntil(3000, [&] { return srv->getClientCount() == 2; });
    if (joined) {
        if (fromWriter) {
            for (int id : srv->getClientIds()) srv->sendAsync(id, string("x"));
        } else {
            ::send(a, "x", 1, 0);
            ::send(b, "x", 1, 0);
        }
    }
    const bool bothReturned = joined && waitUntil(5000, [&] { return returned.load() == 2; });
    check((prefix + ": both calls return").c_str(), bothReturned);
    // closedAfterData: with `fromWriter` the byte each writer sent comes first
    check((prefix + ": both clients are disconnected").c_str(),
          joined && closedAfterData(a, 3000) && closedAfterData(b, 3000) &&
          waitUntil(3000, [&] { return srv->getClientCount() == 0; }));
    check((prefix + ": the server keeps running").c_str(), srv->isRunning());

    if (bothReturned) {
        server.reset();
        check((prefix + ": the server is destroyed afterwards").c_str(), true);
    } else {
        server.release();
    }
    if (a != kBadSocket) TC_CLOSE(a);
    if (b != kBadSocket) TC_CLOSE(b);
}

// disconnectAllClients() on this thread while a client connects: the first
// client's onReceive listener holds its disconnect until the call has
// unregistered it, then connects a second client and waits until the server
// has registered it. The call must not wait for the threads of that second
// client, which stays connected.
static void disconnectAllWhileAClientConnects() {
    const char* const prefix = "teardown: disconnectAllClients() while a client connects";
    Watchdog dog(prefix, 20000);

    TcpServer server;
    atomic<int> firstId{-1}, secondId{-1};
    atomic<bool> listenerIn{false}, secondJoined{false};
    atomic<rawsocket_t> second{kBadSocket};
    int port = 0;
    EventListener onCon = server.onClientConnect.listen([&](TcpClientConnectEventArgs& e) {
        if (firstId.load() < 0) firstId = e.clientId;
        else secondId = e.clientId;
    });
    EventListener onRecv = server.onReceive.listen([&](TcpServerReceiveEventArgs& e) {
        if (e.clientId != firstId.load() || listenerIn.exchange(true)) return;
        waitUntil(3000, [&] { return server.getClientCount() == 0; });
        second = connectTo(port);
        secondJoined = waitUntil(3000, [&] { return secondId.load() >= 0 &&
                                                    server.getClientCount() == 1; });
    });

    port = startOnFreePort(server, -1);
    check((string(prefix) + ": server started").c_str(), port != 0);
    if (!port) return;

    rawsocket_t first = connectTo(port);
    const bool joined = first != kBadSocket &&
                        waitUntil(3000, [&] { return server.getClientCount() == 1; });
    if (joined) ::send(first, "x", 1, 0);
    const bool held = joined && waitUntil(3000, [&] { return listenerIn.load(); });
    if (held) server.disconnectAllClients();

    check((string(prefix) + ": it returns").c_str(), held);
    check((string(prefix) + ": the new client connected meanwhile").c_str(), secondJoined.load());
    check((string(prefix) + ": the first client is disconnected").c_str(),
          first != kBadSocket && closedByServer(first, 3000));
    check((string(prefix) + ": the new client stays connected").c_str(),
          second.load() != kBadSocket && !closedByServer(second.load(), 200) &&
          server.getClientCount() == 1);

    server.stop();
    if (first != kBadSocket) TC_CLOSE(first);
    if (second.load() != kBadSocket) TC_CLOSE(second.load());
}

static void testListenerTeardownThreads() {
    stopFromOnErrorAfterSendTimeout();
    destroyWhileWriterListenerRuns(false);
    destroyWhileWriterListenerRuns(true);
    crossDisconnectFromTwoListeners();
    disconnectAllFromTwoListeners(false);
    disconnectAllFromTwoListeners(true);
    disconnectAllWhileAClientConnects();
}

// -----------------------------------------------------------------------------
// stop() on several threads at once
// -----------------------------------------------------------------------------

// stop() from onClientConnect, on the accept thread, together with a stop()
// from the first client's thread (its receive thread, or its writer with
// `fromWriter`), which is parked in a listener while the accept thread is in
// onClientConnect for the second client.
//
// By default the accept thread goes first. With `acceptLast` the other thread
// goes first, and the accept thread calls stop() only once that one has
// cleared isRunning(). Neither stop() waits for any thread: a receive or
// writer thread's stop() does not take the accept thread either, so that is
// left to the destruction below. stopOnAcceptThreadAfterAnotherTookIt()
// covers an accept thread whose stop() comes after another has taken it.
static void concurrentStopWithAcceptThread(bool fromWriter, bool acceptLast) {
    const char* const other = fromWriter ? "onSendComplete" : "onReceive";
    const string prefix = acceptLast
        ? string("concurrent stop: ") + other + ", then onClientConnect"
        : string("concurrent stop: onClientConnect + ") + other;
    // Declared first, so it also covers the server's destruction below
    Watchdog dog(prefix.c_str(), 20000);

    auto server = make_unique<TcpServer>();
    TcpServer* srv = server.get();
    atomic<int> firstId{-1};
    atomic<bool> connectIn{false}, otherIn{false};
    atomic<bool> connectStopped{false}, otherStopped{false};
    atomic<bool> sawOtherStopped{false};

    EventListener onCon = srv->onClientConnect.listen([&, srv](TcpClientConnectEventArgs& e) {
        if (firstId.load() < 0) {
            firstId = e.clientId;
            return;
        }
        connectIn = true;
        if (acceptLast) {
            // The other stop() has cleared isRunning(). It must return while
            // this thread is still in the listener: a stop() that waited for
            // the accept thread would wait here until the timeout
            waitUntil(3000, [&] { return !srv->isRunning(); });
            sawOtherStopped = waitUntil(3000, [&] { return otherStopped.load(); });
        } else {
            waitUntil(3000, [&] { return otherIn.load(); });
        }
        srv->stop();
        connectStopped = true;
    });
    // The first client's thread, parked in a listener until the accept thread
    // is in onClientConnect for the second client
    auto otherListener = [&, srv](int clientId) {
        if (clientId != firstId.load() || otherIn.exchange(true)) return;
        waitUntil(3000, [&] { return connectIn.load(); });
        srv->stop();
        otherStopped = true;
    };
    EventListener onRecv = srv->onReceive.listen([&](TcpServerReceiveEventArgs& e) {
        if (!fromWriter) otherListener(e.clientId);
    });
    EventListener onSent = srv->onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (fromWriter) otherListener(e.clientId);
    });

    const int port = startOnFreePort(*srv, -1);
    check((prefix + ": server started").c_str(), port != 0);
    if (!port) return;

    rawsocket_t first = connectTo(port);
    const bool joined = first != kBadSocket &&
                        waitUntil(3000, [&] { return srv->getClientCount() == 1 &&
                                                     firstId.load() >= 0; });
    if (joined) {
        if (fromWriter) srv->sendAsync(firstId.load(), string("x"));
        else ::send(first, "x", 1, 0);
    }
    const bool parked = joined && waitUntil(3000, [&] { return otherIn.load(); });
    rawsocket_t second = parked ? connectTo(port) : kBadSocket;

    const bool bothReturned =
        parked && second != kBadSocket &&
        waitUntil(5000, [&] { return connectStopped.load() && otherStopped.load(); });
    check((prefix + ": both stop() calls return").c_str(), bothReturned);
    if (acceptLast) {
        check((prefix + ": the other stop() does not wait for the accept thread").c_str(),
              sawOtherStopped.load());
    }
    check((prefix + ": the server is stopped").c_str(), !srv->isRunning());
    // The byte the writer sent comes first; this reads it
    if (fromWriter && first != kBadSocket) closedByServer(first, 3000);
    check((prefix + ": every client is disconnected").c_str(),
          first != kBadSocket && closedByServer(first, 3000) &&
          second != kBadSocket && closedByServer(second, 3000) &&
          waitUntil(3000, [&] { return srv->getClientCount() == 0; }));

    if (bothReturned) {
        server.reset();
        check((prefix + ": the server is destroyed afterwards").c_str(), true);
    } else {
        // Its threads are stuck waiting on each other and still use it
        server.release();
    }
    if (first != kBadSocket) TC_CLOSE(first);
    if (second != kBadSocket) TC_CLOSE(second);
}

// stop() from onClientConnect after a stop() on a plain thread has taken the
// accept thread out of the server and is waiting for it to end. The accept
// thread then has to recognize itself without it: its stop() still closes the
// listening socket before it returns and waits for nothing, and the plain
// thread's stop() returns once the listener is done.
static void stopOnAcceptThreadAfterAnotherTookIt() {
    const char* const prefix = "concurrent stop: onClientConnect during a plain stop()";
    Watchdog dog(prefix, 20000);

    auto server = make_unique<TcpServer>();
    TcpServer* srv = server.get();
    atomic<int> connects{0}, oldPort{0};
    atomic<bool> plainIn{false}, listenerStopped{false}, refusedAfterStop{false};
    atomic<bool> plainTook{false}, sawPlainTook{false};
#ifdef __linux__
    atomic<unsigned long> listenInode{0};
    atomic<bool> socketClosed{false};
#endif
    EventListener onCon = srv->onClientConnect.listen([&, srv](TcpClientConnectEventArgs&) {
        if (connects++ != 0) return;
        // The plain thread's stop() has taken the accept thread out of the
        // server and is about to wait for it (the accept-taken hook). Without
        // that, this stop() could come first and the case would not cover
        // what it is for
        sawPlainTook = waitUntil(3000, [&] { return plainTook.load(); });
        srv->stop();
        // Nothing may be listening on the old port any more
        rawsocket_t probe = connectTo(oldPort.load());
        refusedAfterStop = probe == kBadSocket;
        if (probe != kBadSocket) TC_CLOSE(probe);
#ifdef __linux__
        // shutdown() by the plain thread's stop() already refuses connections
        // on Linux, so also check that the descriptor itself is gone
        socketClosed = listenInode.load() != 0 && !holdsSocket(listenInode.load());
#endif
        listenerStopped = true;
    });

    const int port = startOnFreePort(*srv, -1);
    check((string(prefix) + ": server started").c_str(), port != 0);
    if (!port) return;
    oldPort = port;
#ifdef __linux__
    listenInode = listeningInode(port);
#endif

    rawsocket_t c = connectTo(port);
    const bool inListener = c != kBadSocket && waitUntil(3000, [&] { return connects.load() == 1; });

    // On the plain thread, once its stop() has taken the accept thread and
    // before it waits for it
    internal::setTcpServerAcceptTakenHookForTests([&] {
        if (plainIn.load()) plainTook = true;
    });
    atomic<bool> plainReturned{false}, stoppedAtReturn{false};
    thread plain([&, srv] {
        if (!inListener) return;
        plainIn = true;
        srv->stop();
        stoppedAtReturn = listenerStopped.load();
        plainReturned = true;
    });
    const bool returned = inListener && waitUntil(5000, [&] { return plainReturned.load(); });
    internal::setTcpServerAcceptTakenHookForTests(nullptr);

    check((string(prefix) + ": the plain stop() takes the accept thread first").c_str(),
          sawPlainTook.load());
    check((string(prefix) + ": the listener's stop() returns").c_str(), listenerStopped.load());
    check((string(prefix) + ": the other stop() returns once the listener is done").c_str(),
          returned && stoppedAtReturn.load());
    check((string(prefix) + ": the old port refuses connections once stop() returns").c_str(),
          refusedAfterStop.load());
#ifdef __linux__
    check((string(prefix) + ": the listening socket is closed once stop() returns").c_str(),
          socketClosed.load());
#endif
    check((string(prefix) + ": the client is disconnected").c_str(),
          c != kBadSocket && closedByServer(c, 3000) && srv->getClientCount() == 0);

    if (returned || !inListener) {
        plain.join();
        server.reset();
        check((string(prefix) + ": the server is destroyed afterwards").c_str(), true);
    } else {
        plain.detach();
        server.release();
    }
    if (c != kBadSocket) TC_CLOSE(c);
}

// stop() from two clients' threads at once: both receive threads (onReceive),
// or both writers (onSendComplete). Each listener waits until the other is in
// its own before calling stop(). Neither may wait for the accept thread or the
// other client's threads: the other one is in a listener too. Both only
// disconnect the clients, and the threads are joined when the server is
// destroyed below.
static void concurrentStopOnTwoClientThreads(bool fromWriter) {
    const string prefix = string("concurrent stop: two ") +
                          (fromWriter ? "onSendComplete" : "onReceive") + " listeners";
    Watchdog dog(prefix.c_str(), 20000);

    auto server = make_unique<TcpServer>();
    TcpServer* srv = server.get();
    atomic<int> inside{0}, stopped{0};
    mutex seenMutex;
    vector<int> seen;   // clients whose listener has run, guarded by seenMutex

    auto listener = [&, srv](int clientId) {
        {
            lock_guard<mutex> lock(seenMutex);
            if (find(seen.begin(), seen.end(), clientId) != seen.end()) return;
            seen.push_back(clientId);
        }
        ++inside;
        waitUntil(3000, [&] { return inside.load() >= 2; });
        srv->stop();
        ++stopped;
    };
    EventListener onRecv = srv->onReceive.listen([&](TcpServerReceiveEventArgs& e) {
        if (!fromWriter) listener(e.clientId);
    });
    EventListener onSent = srv->onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (fromWriter) listener(e.clientId);
    });

    const int port = startOnFreePort(*srv, -1);
    check((prefix + ": server started").c_str(), port != 0);
    if (!port) return;

    rawsocket_t a = connectTo(port);
    rawsocket_t b = connectTo(port);
    const bool joined = a != kBadSocket && b != kBadSocket &&
                        waitUntil(3000, [&] { return srv->getClientCount() == 2; });
    if (joined) {
        if (fromWriter) {
            for (int id : srv->getClientIds()) srv->sendAsync(id, string("x"));
        } else {
            ::send(a, "x", 1, 0);
            ::send(b, "x", 1, 0);
        }
    }

    const bool bothReturned = joined && waitUntil(5000, [&] { return stopped.load() == 2; });
    check((prefix + ": both stop() calls return").c_str(), bothReturned);
    check((prefix + ": the server is stopped").c_str(), !srv->isRunning());
    // The byte each writer sent comes first; this reads it
    if (fromWriter && joined) {
        closedByServer(a, 3000);
        closedByServer(b, 3000);
    }
    check((prefix + ": every client is disconnected").c_str(),
          joined && closedByServer(a, 3000) && closedByServer(b, 3000) &&
          waitUntil(3000, [&] { return srv->getClientCount() == 0; }));

    if (bothReturned) {
        server.reset();
        check((prefix + ": the server is destroyed afterwards").c_str(), true);
    } else {
        server.release();
    }
    if (a != kBadSocket) TC_CLOSE(a);
    if (b != kBadSocket) TC_CLOSE(b);
}

// stop() on a plain thread while a writer's onSendComplete listener calls
// stop() too. The stop hook holds the plain thread's stop() right after it has
// joined the accept thread, and the listener calls stop() only then, so both
// are under way at once. The listener keeps running after its stop() returns;
// the plain thread's stop() has to wait for it, or destroying the server right
// after would pull the server out from under that writer.
static void concurrentStopOnPlainAndWriterThread() {
    const char* const prefix = "concurrent stop: another thread + onSendComplete";
    Watchdog dog(prefix, 20000);

    auto server = make_unique<TcpServer>();
    TcpServer* srv = server.get();
    atomic<int> firstId{-1};
    atomic<bool> hookArmed{false}, plainTook{false}, tookFirst{false};
    atomic<bool> writerIn{false}, writerStopped{false}, writerDone{false};

    EventListener onCon = srv->onClientConnect.listen([&](TcpClientConnectEventArgs& e) {
        if (firstId.load() < 0) firstId = e.clientId;
    });
    EventListener onSent = srv->onSendComplete.listen([&, srv](TcpSendCompleteEventArgs& e) {
        if (e.clientId != firstId.load() || writerIn.exchange(true)) return;
        tookFirst = waitUntil(3000, [&] { return plainTook.load(); });
        srv->stop();
        writerStopped = true;
        this_thread::sleep_for(chrono::milliseconds(300));
        writerDone = true;
    });

    const int port = startOnFreePort(*srv, -1);
    check((string(prefix) + ": server started").c_str(), port != 0);
    if (!port) return;

    // On the plain thread, once its stop() has joined the accept thread: held
    // until the writer's stop() has returned
    internal::setTcpServerStopHookForTests([&] {
        if (!hookArmed.exchange(false)) return;
        plainTook = true;
        waitUntil(3000, [&] { return writerStopped.load(); });
    });

    rawsocket_t first = connectTo(port);
    const bool joined = first != kBadSocket &&
                        waitUntil(3000, [&] { return srv->getClientCount() == 1 &&
                                                     firstId.load() >= 0; });
    if (joined) srv->sendAsync(firstId.load(), string("x"));
    const bool parked = joined && waitUntil(3000, [&] { return writerIn.load(); });

    atomic<bool> plainReturned{false}, doneAtReturn{false};
    hookArmed = parked;
    thread plain([&] {
        srv->stop();
        doneAtReturn = writerDone.load();
        plainReturned = true;
    });
    const bool writerReturned = parked && waitUntil(5000, [&] { return writerStopped.load(); });
    const bool returned = waitUntil(5000, [&] { return plainReturned.load(); });
    internal::setTcpServerStopHookForTests(nullptr);

    check((string(prefix) + ": the writer calls stop() during the other one").c_str(),
          tookFirst.load());
    check((string(prefix) + ": the writer's stop() returns").c_str(), writerReturned);
    check((string(prefix) + ": the other stop() returns").c_str(), returned);
    check((string(prefix) + ": ...once that writer's listener is done").c_str(),
          returned && doneAtReturn.load());
    check((string(prefix) + ": the client is disconnected").c_str(),
          first != kBadSocket && closedAfterData(first, 3000) && srv->getClientCount() == 0);

    if (returned) {
        plain.join();
        server.reset();
        check((string(prefix) + ": the server is destroyed afterwards").c_str(), true);
    } else {
        plain.detach();
        server.release();
    }
    if (first != kBadSocket) TC_CLOSE(first);
}

// Two plain threads calling stop() at the same moment, while the accept thread
// is held in a listener so that both of them are in stop() before it can end
static void concurrentStopFromTwoThreads() {
    Watchdog dog("concurrent stop: two threads", 20000);

    TcpServer server;
    atomic<bool> inListener{false}, release{false};
    EventListener onCon = server.onClientConnect.listen([&](TcpClientConnectEventArgs&) {
        inListener = true;
        waitUntil(5000, [&] { return release.load(); });
    });
    const int port = startOnFreePort(server, -1);
    check("concurrent stop: two threads: server started", port != 0);
    if (!port) return;

    rawsocket_t c = connectTo(port);
    const bool held = c != kBadSocket && waitUntil(3000, [&] { return inListener.load(); });

    atomic<int> returned{0}, threw{0};
    auto stopper = [&] {
        try {
            server.stop();
        } catch (...) {
            ++threw;
        }
        ++returned;
    };
    thread a(stopper), b(stopper);
    this_thread::sleep_for(chrono::milliseconds(300));   // both are inside stop() by now
    release = true;
    a.join();
    b.join();

    check("concurrent stop: two threads calling stop() at once both return",
          held && returned.load() == 2);
    check("concurrent stop: neither of them throws", threw.load() == 0);
    check("concurrent stop: the client is disconnected",
          held && closedByServer(c, 3000) && server.getClientCount() == 0);
    if (c != kBadSocket) TC_CLOSE(c);
}

// start() while a stop() on another thread is still waiting for the accept
// thread (held in a listener). The old accept thread must be gone before
// start() sets up the new listening socket, or it would carry on with it.
static void startWhileAnotherThreadStops() {
    const char* const prefix = "concurrent stop: start() during another thread's stop()";
    Watchdog dog(prefix, 20000);

    TcpServer server;
    atomic<bool> holdNext{true}, held{false}, release{false};
    EventListener onCon = server.onClientConnect.listen([&](TcpClientConnectEventArgs&) {
        if (!holdNext.exchange(false)) return;
        held = true;
        waitUntil(5000, [&] { return release.load(); });
    });
    const int port = startOnFreePort(server, -1);
    check((string(prefix) + ": server started").c_str(), port != 0);
    if (!port) return;

    rawsocket_t c = connectTo(port);
    const bool ready = c != kBadSocket && waitUntil(3000, [&] { return held.load(); });

    atomic<bool> stopReturned{false};
    thread stopper([&] {
        server.stop();
        stopReturned = true;
    });
    // Once that stop() is waiting for the accept thread, let the accept thread
    // go a little later, from a third thread: start() below may wait for it
    waitUntil(3000, [&] { return !server.isRunning(); });
    this_thread::sleep_for(chrono::milliseconds(200));
    thread releaser([&] {
        this_thread::sleep_for(chrono::milliseconds(300));
        release = true;
    });

    const int again = ready ? startOnFreePort(server, -1) : 0;
    check((string(prefix) + ": start() succeeds").c_str(), again != 0);
    check((string(prefix) + ": the other stop() returns").c_str(),
          waitUntil(3000, [&] { return stopReturned.load(); }));
    rawsocket_t d = again ? connectTo(again) : kBadSocket;
    check((string(prefix) + ": the restarted server accepts a client").c_str(),
          d != kBadSocket && waitUntil(3000, [&] { return server.getClientCount() == 1; }));

    releaser.join();
    server.stop();
    stopper.join();
    if (c != kBadSocket) TC_CLOSE(c);
    if (d != kBadSocket) TC_CLOSE(d);
}

// start() while a stop() on another thread has joined the accept thread but
// not yet disconnected the clients and joined their threads (held there by the
// stop hook). start() has to wait for that stop() to finish, or the stop()
// would go on to disconnect the restarted server's clients. Without the wait,
// start() returns while the stop() is still held; the test then connects a
// new client before letting the stop() go, so the stop() gets to it.
static void startWhileAnotherStopCleansUp() {
    const char* const prefix = "concurrent stop: start() during another stop()'s cleanup";
    Watchdog dog(prefix, 20000);

    TcpServer server;
    atomic<bool> holdNext{false}, held{false}, release{false};
    internal::setTcpServerStopHookForTests([&] {
        if (!holdNext.exchange(false)) return;
        held = true;
        waitUntil(5000, [&] { return release.load(); });
    });
    atomic<int> disconnects{0};
    EventListener onDis = server.onClientDisconnect.listen([&](TcpClientDisconnectEventArgs&) {
        ++disconnects;
    });

    const int port = startOnFreePort(server, -1);
    check((string(prefix) + ": server started").c_str(), port != 0);
    if (!port) {
        internal::setTcpServerStopHookForTests(nullptr);
        return;
    }

    holdNext = true;
    atomic<bool> stopReturned{false};
    thread stopper([&] {
        server.stop();
        stopReturned = true;
    });
    const bool isHeld = waitUntil(3000, [&] { return held.load(); });
    check((string(prefix) + ": the other stop() is held after its join").c_str(), isHeld);

    atomic<int> again{-1};
    thread starter([&] { again = startOnFreePort(server, -1); });

    // start() must still be waiting while the other stop() is held. Should it
    // return anyway, a client connects before that stop() goes on.
    const bool startedEarly = waitUntil(500, [&] { return again.load() >= 0; });
    check((string(prefix) + ": start() waits for the other stop() to finish").c_str(),
          isHeld && !startedEarly);
    rawsocket_t d = kBadSocket;
    if (startedEarly && again.load() > 0) {
        d = connectTo(again.load());
        waitUntil(3000, [&] { return server.getClientCount() == 1; });
    }
    release = true;
    starter.join();
    check((string(prefix) + ": start() succeeds").c_str(), again.load() > 0);
    check((string(prefix) + ": the other stop() returns").c_str(),
          waitUntil(3000, [&] { return stopReturned.load(); }));
    stopper.join();
    internal::setTcpServerStopHookForTests(nullptr);

    if (d == kBadSocket && again.load() > 0) d = connectTo(again.load());
    const bool joined = d != kBadSocket &&
                        waitUntil(3000, [&] { return server.getClientCount() == 1; });
    check((string(prefix) + ": the restarted server accepts a client").c_str(), joined);
    check((string(prefix) + ": that client stays connected").c_str(),
          joined && !closedByServer(d, 300) && server.isRunning() &&
          server.getClientCount() == 1);
    check((string(prefix) + ": and gets no onClientDisconnect").c_str(), disconnects.load() == 0);

    server.stop();
    if (d != kBadSocket) TC_CLOSE(d);
}

// start() from an onReceive listener, on a client's receive thread, is
// refused: it would have to wait for a stop() that may be joining that very
// thread. The server keeps running and the client stays connected.
static void startFromClientThreadRefused() {
    const char* const prefix = "concurrent stop: start() from onReceive";
    Watchdog dog(prefix, 20000);

    TcpServer server;
    atomic<bool> listenerDone{false}, refused{false};
    EventListener onRecv = server.onReceive.listen([&](TcpServerReceiveEventArgs&) {
        refused = !server.start(freePort());
        listenerDone = true;
    });
    const int port = startOnFreePort(server, -1);
    check((string(prefix) + ": server started").c_str(), port != 0);
    if (!port) return;

    rawsocket_t c = connectTo(port);
    const bool joined = c != kBadSocket &&
                        waitUntil(3000, [&] { return server.getClientCount() == 1; });
    if (joined) ::send(c, "x", 1, 0);
    check((string(prefix) + ": the listener returns").c_str(),
          joined && waitUntil(3000, [&] { return listenerDone.load(); }));
    check((string(prefix) + ": start() is refused").c_str(), refused.load());
    check((string(prefix) + ": the server keeps running").c_str(),
          server.isRunning() && server.getClientCount() == 1 && !closedByServer(c, 200));

    server.stop();
    if (c != kBadSocket) TC_CLOSE(c);
}

static void testConcurrentStop() {
    concurrentStopWithAcceptThread(false, false);
    concurrentStopWithAcceptThread(true, false);
    concurrentStopWithAcceptThread(false, true);
    concurrentStopWithAcceptThread(true, true);
    stopOnAcceptThreadAfterAnotherTookIt();
    concurrentStopOnTwoClientThreads(false);
    concurrentStopOnTwoClientThreads(true);
    concurrentStopOnPlainAndWriterThread();
    concurrentStopFromTwoThreads();
    startWhileAnotherThreadStops();
    startWhileAnotherStopCleansUp();
    startFromClientThreadRefused();
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

    // Still failing once the throttle interval (5 s) is over: reported again,
    // not only counted
    check("accept errors: reported again while the condition lasts",
          waitUntil(6000, [&] { return errors.load() >= 2; }));
    printf("  (%d onError call(s) by now)\n", errors.load());

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

// -----------------------------------------------------------------------------
// A receive thread that cannot be started disconnects the client it was for
// (runs in a forked child: it replaces pthread_create for the whole process)
// -----------------------------------------------------------------------------
static void receiveThreadFailureChild() {
    WarningCounter warnings;
    TcpServer server;
    atomic<int> connects{0}, disconnects{0}, connectId{-1}, disconnectId{-1};
    atomic<int> errors{0}, lastErrorClientId{-2};
    // The client whose receive thread fails gets an empty payload queued as
    // it is announced (empty, so no write can fail and add an onError). Its
    // writer's onSendComplete listener then waits for the disconnect the
    // accept thread reports: that thread must not wait for the writer in
    // between.
    atomic<bool> probeNext{false}, writerProbeDone{false}, writerSawDisconnect{false};
    EventListener onCon = server.onClientConnect.listen([&](TcpClientConnectEventArgs& e) {
        connectId = e.clientId;
        ++connects;
        if (probeNext.exchange(false)) server.sendAsync(e.clientId, string());
    });
    EventListener onSent = server.onSendComplete.listen([&](TcpSendCompleteEventArgs& e) {
        if (e.clientId != connectId.load()) return;
        writerSawDisconnect = waitUntil(2000, [&] { return disconnects.load() == 1; });
        writerProbeDone = true;
    });
    EventListener onDis = server.onClientDisconnect.listen([&](TcpClientDisconnectEventArgs& e) {
        disconnectId = e.clientId;
        ++disconnects;
    });
    EventListener onErr = server.onError.listen([&](TcpServerErrorEventArgs& e) {
        lastErrorClientId = e.clientId;
        ++errors;
    });
    const int port = startOnFreePort(server, -1);
    check("receive thread: server started", port != 0);
    if (!port) return;

    // First the writer thread fails (the next creation), which is reported...
    g_threadStartsBeforeFailure = 0;
    rawsocket_t a = connectTo(port);
    check("receive thread: a failed writer closes the connection",
          a != kBadSocket && closedByServer(a, 3000));
    check("receive thread: the writer failure is reported",
          waitUntil(1000, [&] { return errors.load() == 1; }) && lastErrorClientId.load() == -1);
    if (a != kBadSocket) TC_CLOSE(a);

    // ...then, well within the throttle interval, the receive thread fails
    // (the writer's creation goes through, the next one does not)
    probeNext = true;
    g_threadStartsBeforeFailure = 1;
    rawsocket_t b = connectTo(port);
    check("receive thread: the connection is closed",
          b != kBadSocket && closedByServer(b, 3000));
    check("receive thread: the client is announced, then disconnected",
          waitUntil(1000, [&] { return connects.load() == 1 && disconnects.load() == 1; }) &&
          connectId.load() == disconnectId.load());
    check("receive thread: no client is left registered", server.getClientCount() == 0);
    check("receive thread: reported through onError for that client, "
          "though another failure was just reported",
          waitUntil(1000, [&] { return errors.load() == 2; }) &&
          lastErrorClientId.load() == connectId.load());
    check("receive thread: the accept thread does not wait for that client's writer",
          waitUntil(3000, [&] { return writerProbeDone.load(); }) && writerSawDisconnect.load());
    if (b != kBadSocket) TC_CLOSE(b);

    // Recovery
    rawsocket_t c = connectTo(port);
    check("receive thread: a client is accepted once threads can start",
          waitUntil(3000, [&] { return server.getClientCount() == 1; }));
    if (c != kBadSocket) TC_CLOSE(c);

    server.stop();
}

// -----------------------------------------------------------------------------
// start() fails cleanly when its accept thread cannot be started
// (runs in a forked child: it replaces pthread_create for the whole process)
// -----------------------------------------------------------------------------
static void acceptThreadFailureChild() {
    TcpServer server;
    atomic<int> errors{0};
    EventListener onErr = server.onError.listen([&](TcpServerErrorEventArgs&) { ++errors; });
    const int port = freePort();
    check("accept thread: a port is free", port != 0);
    if (!port) return;

    g_threadStartsBeforeFailure = 0;   // the accept thread is the next one
    check("accept thread: start() returns false", !server.start(port));
    check("accept thread: reported through onError once", errors.load() == 1);
    check("accept thread: the server is not running", !server.isRunning());
    rawsocket_t probe = connectTo(port);
    check("accept thread: nothing listens on the port", probe == kBadSocket);
    if (probe != kBadSocket) TC_CLOSE(probe);

    // Recovery, on the same port
    check("accept thread: start() on that port succeeds afterwards", server.start(port));
    rawsocket_t c = connectTo(port);
    check("accept thread: and accepts a client",
          c != kBadSocket && waitUntil(3000, [&] { return server.getClientCount() == 1; }));
    if (c != kBadSocket) TC_CLOSE(c);
    server.stop();
}
#endif

int main() {
#ifdef _WIN32
    // The test's own sockets (the free-port probe, raw clients) need Winsock
    // even while no TcpServer or TcpClient exists.
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
#ifdef __linux__
    // Forked first, while this process has no threads of its own yet
    inChild("accept errors back off (child)", acceptBackoffChild);
    if (geteuid() == 0) {
        skip("a failed thread start closes the connection (child)",
             "root is not bound by RLIMIT_NPROC");
    } else {
        inChild("a failed thread start closes the connection (child)", threadStartFailureChild);
    }
    inChild("a failed receive thread disconnects its client (child)", receiveThreadFailureChild);
    inChild("a failed accept thread fails start() (child)", acceptThreadFailureChild);
#else
    skip("accept errors back off", "Linux only (fork + RLIMIT_NOFILE)");
    skip("a failed thread start closes the connection", "Linux only (fork + RLIMIT_NPROC)");
    skip("a failed receive thread disconnects its client", "Linux only (fork + pthread_create)");
    skip("a failed accept thread fails start()", "Linux only (fork + pthread_create)");
#endif

    testReclaim();
    testLimit();
    testDefaultUnlimited();
    testListenerTeardown();
    testListenerTeardownThreads();
    testConcurrentStop();

    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
