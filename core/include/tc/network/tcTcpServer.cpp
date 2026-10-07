// =============================================================================
// tcTcpServer.cpp - TCP server socket implementation
// =============================================================================

#include "tc/network/tcTcpServer.h"
#include "tc/network/tcSocketInternal.h"
#include "tc/utils/tcLog.h"
#include <algorithm>
#include <cstring>
#include <system_error>

#ifdef _WIN32
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
#else
    #include <errno.h>
    #define CLOSE_SOCKET ::close
    #define SOCKET_ERROR_CODE errno
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#ifndef _WIN32
    #include <poll.h>       // poll(), for the send/receive waits below
#endif
#include <chrono>

// SIGPIPE: sends pass TC_SEND_FLAGS and every accepted socket gets
// SO_NOSIGPIPE (see tcSocketInternal.h), so a peer that closed first makes
// send() fail instead of killing the process.

namespace trussc {

namespace {

#ifdef _WIN32
using socket_t = SOCKET;
#else
using socket_t = int;
#endif

// Accepted sockets are non-blocking, and both the send loop and the receive
// thread wait in slices instead of parking in the kernel. A blocking call
// cannot be interrupted portably: Winsock's shutdown() does not wake a send()
// that is already parked, so disconnecting a peer that stopped reading hung
// until that peer moved. Waiting in slices lets either loop notice the channel
// closing on its own, which is the same on every platform.
bool setNonBlocking(socket_t s) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// Wait for the socket to become writable (forWrite) or readable, up to
// sliceMs. Returns 1 ready, 0 timed out, -1 socket error.
int waitReady(socket_t s, bool forWrite, int sliceMs) {
#ifdef _WIN32
    fd_set fds, exceptfds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    FD_ZERO(&exceptfds);
    FD_SET(s, &exceptfds);
    struct timeval tv;
    tv.tv_sec = sliceMs / 1000;
    tv.tv_usec = (sliceMs % 1000) * 1000;
    int res = ::select(0, forWrite ? NULL : &fds, forWrite ? &fds : NULL, &exceptfds, &tv);
    if (res <= 0) return res == 0 ? 0 : -1;
    return FD_ISSET(s, &exceptfds) ? -1 : 1;
#else
    struct pollfd pfd;
    pfd.fd = s;
    pfd.events = static_cast<short>(forWrite ? POLLOUT : POLLIN);
    pfd.revents = 0;
    int res = ::poll(&pfd, 1, sliceMs);
    if (res < 0) return errno == EINTR ? 0 : -1;
    if (res == 0) return 0;
    if (pfd.revents & (POLLERR | POLLNVAL)) return -1;
    return 1;
#endif
}

// The error a failed wait left on the socket. poll() reports POLLERR without
// touching errno, so read it from the socket rather than guessing.
int socketError(socket_t s) {
    int err = 0;
#ifdef _WIN32
    int len = static_cast<int>(sizeof(err));
    if (::getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len) != 0) return SOCKET_ERROR_CODE;
#else
    socklen_t len = sizeof(err);
    if (::getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) != 0) return SOCKET_ERROR_CODE;
#endif
    return err;
}

// How long a wait parks before re-checking whether the channel is still open.
//
// A longer value was tried, on the theory that a disconnect shuts the socket
// down before it is closed and a shutdown socket is reported ready at once, so
// the timeout would never be what ends a wait. Measured across CI at 10 s:
//
//   Linux    disconnectClient  5 ms    shutdown() wakes poll()
//   macOS    disconnectClient 35 ms    shutdown() wakes poll()
//   Windows  disconnectClient  4+ s    shutdown() does NOT wake select()
//
// So Winsock declines to wake a waiting select() for the same reason it declines
// to wake a parked send(), and on Windows this slice is not a backstop at all —
// it is the mechanism. macOS additionally hung a full slice per round in the
// teardown stress case, which points at a receive thread polling a descriptor
// closed and recycled under it between its `open` check and its wait; the short
// slice bounds that too. Both want fixing before this number can grow.
constexpr int kWaitSliceMs = 100;

// listen() backlog: connections the kernel has completed but the accept loop
// has not taken yet. The loop takes them as fast as they arrive, so this only
// has to absorb a burst of simultaneous connects. It is not a client limit
// (maxClients is), which is why it no longer follows maxClients. 128 is the
// traditional SOMAXCONN; the kernel may clamp it further (Linux:
// net.core.somaxconn).
constexpr int kListenBacklog = 128;

// Back-off after a failed accept(). An error such as running out of descriptors
// leaves the pending connection queued, so the listening socket stays readable
// and retrying at once would spin a core. Doubles from min to max and resets on
// the next connection that is accepted.
constexpr int kAcceptBackoffMinMs = 10;
constexpr int kAcceptBackoffMaxMs = 100;

// Errors that concern one pending connection rather than the server: the next
// accept() is expected to succeed, so they are retried without a back-off.
bool isTransientAcceptError(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAECONNRESET || err == WSAEINTR;
#else
    return err == EWOULDBLOCK || err == EAGAIN || err == EINTR || err == ECONNABORTED;
#endif
}

// Lets a repeated warning through at most once per interval. What it holds back
// is counted and handed out as one summary once the interval is over, and the
// next occurrence after that is let through again, so a condition that lasts
// costs about two lines per interval however often it recurs. Used only by the
// accept thread, so it needs no locking.
class LogThrottle {
public:
    static constexpr std::chrono::seconds kInterval{5};

    // True when this occurrence should be logged now
    bool admit() {
        const auto now = std::chrono::steady_clock::now();
        if (logged_ && now - last_ < kInterval) {
            ++held_;
            return false;
        }
        logged_ = true;
        last_ = now;
        return true;
    }

    // How many occurrences were held back, once the interval since the last
    // admitted one is over; 0 otherwise. The caller logs the summary. It does
    // not restart the interval: the next occurrence is admitted, so one that
    // keeps recurring is still reported once per interval rather than only as
    // a count.
    int takeHeld() {
        if (held_ == 0) return 0;
        const auto now = std::chrono::steady_clock::now();
        if (now - last_ < kInterval) return 0;
        const int n = held_;
        held_ = 0;
        return n;
    }

private:
    std::chrono::steady_clock::time_point last_{};
    bool logged_ = false;
    int held_ = 0;
};

// The server whose accept thread this is, null on every other thread. stop()
// and start() behave differently there, and asking the std::thread object
// instead would race with a stop() on another thread moving it out to join it.
thread_local const TcpServer* tlAcceptThreadOf = nullptr;

// The server whose client thread (receive or writer) this is, null on every
// other thread. start() there must not wait for another stop() to finish:
// that one may be waiting for this very thread.
thread_local const TcpServer* tlClientThreadOf = nullptr;

// Whether the caller is one of this server's own threads (accept, receive or
// writer). Such a thread never joins another of the server's threads: that
// thread may be in a listener waiting for the caller, directly or through a
// third one, and nothing on either side would give way. Teardown there only
// unregisters clients and shuts their channels; the threads are joined by the
// accept loop once they have finished (reapClientThreads), or by stop(),
// start() or the destructor on some other thread.
bool isServerThread(const TcpServer* server) {
    return tlAcceptThreadOf == server || tlClientThreadOf == server;
}

// Set by internal::setTcpServerStopHookForTests() and
// internal::setTcpServerAcceptTakenHookForTests(), empty otherwise
std::mutex g_stopHookMutex;
std::function<void()> g_stopHook;
std::function<void()> g_acceptTakenHook;

void runHookForTests(const std::function<void()>& slot) {
    std::function<void()> hook;
    {
        std::lock_guard<std::mutex> lock(g_stopHookMutex);
        hook = slot;
    }
    if (hook) hook();
}

void runStopHookForTests() { runHookForTests(g_stopHook); }
void runAcceptTakenHookForTests() { runHookForTests(g_acceptTakenHook); }

} // namespace

namespace internal {

void setTcpServerStopHookForTests(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(g_stopHookMutex);
    g_stopHook = std::move(fn);
}

void setTcpServerAcceptTakenHookForTests(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(g_stopHookMutex);
    g_acceptTakenHook = std::move(fn);
}

} // namespace internal

// =============================================================================
// Constructor / Destructor
// =============================================================================
TcpServer::TcpServer() {
    internal::ensureWinsock();
}

TcpServer::~TcpServer() {
    stop();
}

// =============================================================================
// Server management
// =============================================================================
bool TcpServer::start(int port, int maxClients) {
    // A listener on one of the server's own threads cannot restart the server
    // from there. On the accept thread, the old accept thread would have to be
    // joined, and it is the caller. On a client thread (receive or writer),
    // start() would have to wait for a stop() on another thread to finish
    // tearing the old session down, and that stop() may be joining the caller.
    if (tlAcceptThreadOf == this || tlClientThreadOf == this) {
        logError() << "TcpServer::start() cannot be called from one of the server's own threads "
                      "(accept, receive or writer)";
        return false;
    }

    // Tear down whatever is left. An accept thread can outlive running_ when
    // stop() was called on one of the server's own threads (a listener), and
    // so can client threads; this is where they get joined then. A no-op
    // otherwise.
    stop();

    // A stop() on another thread may still be tearing the old session down:
    // waiting for the old accept thread, which this stop() then found gone,
    // or disconnecting and joining the old clients after it. The accept thread
    // still uses serverSocket_ and running_ and would pick up the new ones,
    // and that stop() would disconnect the new clients and join their
    // threads, so wait for it to finish first.
    {
        std::unique_lock<std::mutex> lock(acceptThreadMutex_);
        stopsDone_.wait(lock, [this] { return stopsInProgress_ == 0; });
    }

    port_ = port;
    maxClients_ = maxClients < 0 ? 0 : maxClients;

    // Create socket
    serverSocket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (serverSocket_ == INVALID_SOCKET) {
#else
    if (serverSocket_ < 0) {
#endif
        notifyError("Failed to create server socket", SOCKET_ERROR_CODE);
        return false;
    }

    // POSIX: SO_REUSEADDR, so a restart can bind the port again right away.
    // Windows: SO_EXCLUSIVEADDRUSE only, so a port another socket holds fails
    // to bind.
#ifdef _WIN32
    BOOL opt = TRUE;
    setsockopt(serverSocket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&opt, sizeof(opt));
#else
    int opt = 1;
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    // Bind
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (::bind(serverSocket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        notifyError("Failed to bind server socket to port " + std::to_string(port), SOCKET_ERROR_CODE);
        CLOSE_SOCKET(serverSocket_);
#ifdef _WIN32
        serverSocket_ = INVALID_SOCKET;
#else
        serverSocket_ = -1;
#endif
        return false;
    }

    // Report the port the socket is bound to, so start(0) gives the one the
    // OS picked
    {
        struct sockaddr_in boundAddr{};
#ifdef _WIN32
        int boundLen = static_cast<int>(sizeof(boundAddr));
#else
        socklen_t boundLen = sizeof(boundAddr);
#endif
        if (::getsockname(serverSocket_, (struct sockaddr*)&boundAddr, &boundLen) == SOCKET_ERROR) {
            notifyError("Failed to get bound server port", SOCKET_ERROR_CODE);
            releaseListenSocket(true);
            return false;
        }
        port_ = ntohs(boundAddr.sin_port);
    }

    // Start listening
    if (::listen(serverSocket_, kListenBacklog) == SOCKET_ERROR) {
        notifyError("Failed to listen on port " + std::to_string(port), SOCKET_ERROR_CODE);
        CLOSE_SOCKET(serverSocket_);
#ifdef _WIN32
        serverSocket_ = INVALID_SOCKET;
#else
        serverSocket_ = -1;
#endif
        return false;
    }

    // The accept loop waits for a pending connection before it calls accept(),
    // but the connection can still vanish in between; a non-blocking socket
    // then returns at once instead of parking the loop until the next one.
    // Accepted sockets are made non-blocking on their own below either way.
    setNonBlocking(serverSocket_);

    // The accept thread owns the listening socket from here on and closes it
    // on its way out, so a thread that cannot be started must close it here.
    running_ = true;
    try {
        std::thread accept(&TcpServer::acceptThreadFunc, this);
        std::lock_guard<std::mutex> lock(acceptThreadMutex_);
        acceptThread_ = std::move(accept);
    } catch (const std::system_error& e) {
        running_ = false;
        releaseListenSocket(true);
        notifyError("Failed to start the accept thread", e.code().value());
        return false;
    }

    logNotice() << "TCP server started on port " << port_;
    return true;
}

void TcpServer::stop() {
    // A server that never ran (or was already stopped) cleans up silently —
    // logging "stopped" from e.g. a static instance's destructor in a CLI
    // that never called start() would be announcing an event that didn't
    // happen. Cleanup below is idempotent either way.
    const bool wasRunning = running_.exchange(false);

    if (tlAcceptThreadOf == this) {
        // Called from the accept thread itself: a listener on an event that
        // fires there (onClientConnect, onError about accepting, or
        // onClientDisconnect for a client whose receive thread could not
        // start). It waits for nothing (see isServerThread()).
        //
        // It is the only thread that uses the listening socket, and it is done
        // with it, so the socket is closed here, before stop() returns.
        // shutdown() alone tears a listener down on Linux only; on macOS and
        // Windows the socket would keep listening, holding the port and
        // completing connections that nobody accepts. Once the listener
        // returns, the loop sees running_ and ends, and the accept thread
        // disconnects every client on its way out (acceptThreadFunc). The
        // client threads and the accept thread itself are joined by the next
        // stop(), start() or the destructor.
        releaseListenSocket(true);
        if (wasRunning) logNotice() << "TCP server stopped";
        return;
    }

    // Wake the accept loop. The loop waits in slices, so it notices running_
    // even where shutdown() does not wake the wait. On its way out it
    // disconnects every client and closes the listening socket, which it
    // alone uses: closing it from here while the loop still waits on it could
    // hand the descriptor to something else first.
    releaseListenSocket(false);

    // A stop() that tears the session down is counted until it has finished,
    // so that start() can wait for it (see there): none of it may run into a
    // server that has been started again. The count is dropped however this
    // stop() leaves, so that start() cannot wait forever, and under the lock:
    // once a start() waiting for it has returned, this thread no longer
    // touches the server.
    //
    // The accept thread is moved out under the same lock, so that of two
    // threads stopping at once only one joins it (the other finds nothing to
    // join), and a start() never finds it gone while the stop() that took it
    // is not counted yet. Joined outside the lock: the accept thread's own
    // stop() never takes it.
    struct StopInProgress {
        TcpServer* server;
        StopInProgress(TcpServer* s, std::thread* takeAccept) : server(s) {
            std::lock_guard<std::mutex> lock(s->acceptThreadMutex_);
            if (takeAccept) *takeAccept = std::move(s->acceptThread_);
            ++s->stopsInProgress_;
        }
        ~StopInProgress() {
            std::lock_guard<std::mutex> lock(server->acceptThreadMutex_);
            --server->stopsInProgress_;
            server->stopsDone_.notify_all();
        }
    };

    if (tlClientThreadOf == this) {
        // A listener on one of this server's receive or writer threads. It
        // joins nothing, not even the accept thread (see isServerThread()):
        // it disconnects every client and returns. The accept loop disconnects
        // any client it registers meanwhile on its way out. The accept thread
        // and the client threads are joined by the next stop() or start() on
        // another thread, or the destructor.
        StopInProgress counted(this, nullptr);
        shutAllClients();
        if (wasRunning) logNotice() << "TCP server stopped";
        return;
    }

    std::thread accept;
    StopInProgress counted(this, &accept);

    if (accept.joinable()) {
        runAcceptTakenHookForTests();
        accept.join();
        runStopHookForTests();
    }

    // Disconnect any client the accept thread has not (a stop() that found no
    // accept thread to join does not wait for it), then join every client
    // thread that is left: this is not a server thread, so it may.
    shutAllClients();
    joinClientThreads();

    if (wasRunning) logNotice() << "TCP server stopped";
}

void TcpServer::releaseListenSocket(bool andClose) {
    // stop() on another thread shuts the socket down while the accept thread
    // closes it (in a stop() of its own, or on its way out). The lock makes
    // them take turns, and whoever closes the socket marks it gone under the
    // same lock, so exactly one of them closes it and neither ever shuts down
    // or closes a descriptor that has been handed out again.
    std::lock_guard<std::mutex> lock(listenSocketMutex_);
    if (serverSocket_ == INVALID_SOCKET) return;
    if (andClose) {
        CLOSE_SOCKET(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    } else {
#ifdef _WIN32
        shutdown(serverSocket_, SD_BOTH);
#else
        shutdown(serverSocket_, SHUT_RDWR);  // Wakes the wait at once on Linux
#endif
    }
}

bool TcpServer::isRunning() const {
    return running_;
}

// =============================================================================
// Accept thread
// =============================================================================
void TcpServer::acceptThreadFunc() {
    tlAcceptThreadOf = this;
    LogThrottle fullLog;    // connections closed because the server is full

    // Each kind of failure has its own throttle, so one that persists does not
    // hide a different one that starts meanwhile, and a summary names what it
    // counted.
    struct Failure {
        const char* what;
        LogThrottle log;
    };
    Failure waitFailed{"Failed to wait for a connection", {}};
    Failure acceptFailed{"Failed to accept a connection", {}};
    Failure writerFailed{"Could not start a thread for a new connection; it was closed", {}};
    Failure readerFailed{"Could not start a receive thread; client disconnected", {}};
    Failure* const failures[] = {&waitFailed, &acceptFailed, &writerFailed, &readerFailed};
    int backoffMs = 0;

    // Resource errors (accept() failing, or no thread for a new client) are
    // logged and reported through onError at most once per throttle interval
    // for each kind, and followed by a growing pause, so a condition that
    // persists costs a few calls a second instead of a core.
    auto backOff = [&](Failure& f, int code, int clientId) {
        backoffMs = backoffMs == 0 ? kAcceptBackoffMinMs
                                   : std::min(backoffMs * 2, kAcceptBackoffMaxMs);
        if (f.log.admit()) {
            logWarning() << "TCP server on port " << port_ << ": " << f.what
                         << " (error " << code << "); backing off";
            notifyError(f.what, code, clientId);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
    };

    while (running_) {
        // Client threads (receive and writer) that have ended are joined here,
        // so they do not pile up until stop() on a server whose clients come
        // and go.
        reapClientThreads();

        if (int held = fullLog.takeHeld()) {
            logWarning() << "TCP server on port " << port_ << ": closed " << held
                         << " more connection(s) while full (maxClients = " << maxClients_ << ")";
        }
        for (Failure* f : failures) {
            if (int held = f->log.takeHeld()) {
                logWarning() << "TCP server on port " << port_ << ": " << held
                             << " more time(s) while backing off: " << f->what;
            }
        }

        // Wait in slices rather than parking in accept(): the loop then gets
        // round to the reaping above even while nobody connects, and notices
        // stop() on platforms where shutdown() does not wake it.
        const int ready = waitReady(serverSocket_, false, kWaitSliceMs);
        if (ready == 0) continue;
        if (!running_) break;
        if (ready < 0) {
            // The wait itself failed. accept() might then report nothing more
            // than "would block", and the loop would be straight back here.
            backOff(waitFailed, socketError(serverSocket_), -1);
            continue;
        }

        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);

#ifdef _WIN32
        SOCKET clientSocket = ::accept(serverSocket_, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET) {
#else
        int clientSocket = ::accept(serverSocket_, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSocket < 0) {
#endif
            // Errors during shutdown are expected and not reported
            const int err = SOCKET_ERROR_CODE;
            if (!running_) break;
            if (isTransientAcceptError(err)) continue;
            backOff(acceptFailed, err, -1);
            continue;
        }

        // Get client information
        char hostStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, hostStr, INET_ADDRSTRLEN);
        int clientPort = ntohs(clientAddr.sin_port);

        // Full: close the new connection at once. Only this thread registers
        // clients, so the count can only have gone down since this check.
        const int limit = maxClients_;
        if (limit > 0 && getClientCount() >= limit) {
            CLOSE_SOCKET(clientSocket);
            if (fullLog.admit()) {
                logWarning() << "TCP server on port " << port_ << " is full (maxClients = " << limit
                             << "); closed a connection from " << hostStr << ":" << clientPort;
            }
            continue;
        }

        internal::setNoSigpipe(clientSocket);

        // SO_SNDTIMEO would bound a blocking send, but nothing can then cut
        // that send short when the client is disconnected. The send loop polls
        // and times out on its own instead.
        if (!setNonBlocking(clientSocket)) {
            logWarning() << "Client socket could not be made non-blocking; "
                            "sends to it will not observe disconnects promptly";
        }

        // Register the client together with its writer thread. The writer is
        // started before the client is announced: a listener on
        // onClientConnect may well send something, and there has to be
        // somebody to drain it. Starting a thread can fail when the process is
        // out of resources; the connection is then closed and reported instead
        // of taking the server down.
        int clientId;
        int threadError = 0;
        bool registered = false;
        {
            std::unique_lock<std::shared_mutex> lock(clientsMutex_);
            clientId = nextClientId_++;
            TcpServerClient client;
            client.id_ = clientId;
            client.host_ = hostStr;
            client.port_ = clientPort;
            client.socket_ = clientSocket;
            client.channel_ = std::make_shared<internal::TcpSendChannel>();
            client.channel_->socket = clientSocket;
            try {
                std::thread writer(&TcpServer::writerThreadFunc, this, clientId, client.channel_);
                clientWriters_.emplace(clientId, std::move(writer));
                clients_[clientId] = client;
                registered = true;
            } catch (const std::system_error& e) {
                threadError = e.code().value();
            }
        }
        if (!registered) {
            // No writer, so nothing else will close the descriptor
            CLOSE_SOCKET(clientSocket);
            backOff(writerFailed, threadError, -1);
            continue;
        }

        logNotice() << "Client " << clientId << " connected from " << hostStr << ":" << clientPort;

        // Notify connection event
        TcpClientConnectEventArgs args;
        args.clientId = clientId;
        args.host = hostStr;
        args.port = clientPort;
        onClientConnect.notify(args);

        // Start receive thread for client
        bool receiving = false;
        {
            std::unique_lock<std::shared_mutex> lock(clientsMutex_);
            try {
                std::thread reader(&TcpServer::clientThreadFunc, this, clientId);
                clientThreads_.emplace(clientId, std::move(reader));
                receiving = true;
            } catch (const std::system_error& e) {
                threadError = e.code().value();
            }
        }
        if (!receiving) {
            // Already announced, so the client leaves the way any other does
            // (unless a listener has disconnected it in the meantime)
            if (removeClient(clientId)) {
                TcpClientDisconnectEventArgs dargs;
                dargs.clientId = clientId;
                dargs.reason = "Could not start a receive thread";
                dargs.wasClean = false;
                onClientDisconnect.notify(dargs);
            }
            backOff(readerFailed, threadError, clientId);
            continue;
        }

        backoffMs = 0;
    }

    // The server is stopping, whether stop() was called here (by a listener)
    // or on another thread. Every client is disconnected before this thread
    // ends, so none is left connected however stop() was reached, but none of
    // their threads is joined (see isServerThread()). Whoever joins this
    // thread joins those.
    shutAllClients();

    // This thread is the only one that uses the listening socket, so it
    // closes it (unless stop() on this thread already has).
    releaseListenSocket(true);
    tlAcceptThreadOf = nullptr;
}

void TcpServer::reapClientThreads() {
    std::vector<std::thread> finished;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        auto take = [&](std::vector<int>& ids, std::unordered_map<int, std::thread>& threads) {
            for (int id : ids) {
                auto it = threads.find(id);
                // Missing: stop(), disconnectClient() or disconnectAllClients()
                // on another thread took it first
                if (it == threads.end()) continue;
                finished.push_back(std::move(it->second));
                threads.erase(it);
            }
            ids.clear();
        };
        take(finishedClientThreads_, clientThreads_);
        take(finishedWriters_, clientWriters_);
    }

    // Outside clientsMutex_, like every other join here. A thread that has
    // announced itself finished may still be unwinding; nobody else needs to
    // wait on the lock for that. It is past its last listener, so this join
    // cannot end up waiting for anything but that.
    for (auto& t : finished) {
        if (t.joinable()) t.join();
    }
}

void TcpServer::joinClientThreads() {
    // Only threads whose client is no longer registered: those are finished or
    // about to be, since their channel has been shut. A client registered
    // after the caller disconnected its clients (by an accept loop that is
    // still running) keeps its threads until it leaves, and joining them here
    // would wait for that.
    std::vector<std::thread> threads;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        auto take = [&](std::unordered_map<int, std::thread>& from, std::vector<int>& finishedIds) {
            for (auto it = from.begin(); it != from.end();) {
                if (clients_.count(it->first)) {
                    ++it;
                    continue;
                }
                threads.push_back(std::move(it->second));
                it = from.erase(it);
            }
            // Ids left behind for the accept loop by threads taken above no
            // longer name anything; those of threads still registered do.
            finishedIds.erase(std::remove_if(finishedIds.begin(), finishedIds.end(),
                                             [&](int id) { return from.count(id) == 0; }),
                              finishedIds.end());
        };
        take(clientThreads_, finishedClientThreads_);
        take(clientWriters_, finishedWriters_);
    }

    // Outside clientsMutex_: the threads being joined take it on their way out
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
}

// =============================================================================
// Client receive thread
// =============================================================================
void TcpServer::clientThreadFunc(int clientId) {
    tlClientThreadOf = this;

    // However this thread ends, it hands its id to the accept loop, which joins
    // it (reapClientThreads). Declared first so it runs last. This touches the
    // server, which is safe because this thread is never detached: whoever
    // tears the server down joins it first. A thread that stop(),
    // disconnectClient() or disconnectAllClients() has already taken is simply
    // not found there.
    struct FinishedMark {
        TcpServer* server;
        int id;
        ~FinishedMark() {
            std::unique_lock<std::shared_mutex> lock(server->clientsMutex_);
            server->finishedClientThreads_.push_back(id);
        }
    } finishedMark{this, clientId};

    std::vector<char> buffer(receiveBufferSize_);

#ifdef _WIN32
    SOCKET clientSocket;
#else
    int clientSocket;
#endif

    {
        std::shared_lock<std::shared_mutex> lock(clientsMutex_);
        auto it = clients_.find(clientId);
        if (it == clients_.end()) return;
        clientSocket = it->second.socket_;
    }

    // Holding the channel keeps this loop's view of the socket's lifetime in
    // step with the send path: `open` is cleared before the descriptor is
    // closed, so checking it first keeps this thread off a closed (or already
    // recycled) descriptor.
    std::shared_ptr<internal::TcpSendChannel> channel = findChannel(clientId);

    while (running_) {
        if (channel && !channel->open) break;

        // The send path gave up on this client (a timeout truncated a payload)
        // and left the removal to this thread.
        if (channel && channel->dropped) {
            removeClient(clientId);
            break;
        }

        // The socket is non-blocking, so wait in slices rather than parking in
        // recv(): this thread then notices the server stopping and the client
        // being disconnected without waiting for traffic that may never come.
        const int ready = waitReady(clientSocket, false, kWaitSliceMs);
        if (ready == 0) continue;
        if (ready < 0) {
            if (running_ && channel && channel->open) {
                TcpClientDisconnectEventArgs args;
                args.clientId = clientId;
                args.reason = "Connection error";
                args.wasClean = false;
                onClientDisconnect.notify(args);

                removeClient(clientId);
            }
            break;
        }

        int received = static_cast<int>(recv(clientSocket, buffer.data(), buffer.size(), 0));

        if (received > 0) {
            TcpServerReceiveEventArgs args;
            args.clientId = clientId;
            args.data.assign(buffer.begin(), buffer.begin() + received);
            onReceive.notify(args);
        } else if (received == 0) {
            // Client closed connection
            TcpClientDisconnectEventArgs args;
            args.clientId = clientId;
            args.reason = "Connection closed by client";
            args.wasClean = true;
            onClientDisconnect.notify(args);

            logNotice() << "Client " << clientId << " disconnected";
            removeClient(clientId);
            break;
        } else {
            // Error
            int err = SOCKET_ERROR_CODE;
#ifdef _WIN32
            if (err == WSAEWOULDBLOCK) continue;
#else
            if (err == EWOULDBLOCK || err == EAGAIN) continue;
#endif
            if (running_) {
                TcpClientDisconnectEventArgs args;
                args.clientId = clientId;
                args.reason = "Connection error";
                args.wasClean = false;
                onClientDisconnect.notify(args);

                removeClient(clientId);
            }
            break;
        }
    }
}

// =============================================================================
// Client management
// =============================================================================
void TcpServer::disconnectClient(int clientId) {
    removeClient(clientId);

    // On one of the server's own threads (a listener) nothing is joined, not
    // even for another client (see isServerThread()): that client's thread may
    // be in a listener disconnecting the caller's client in turn. The threads
    // stay registered; the accept loop joins them once they have ended, and
    // stop(), start() or the destructor on another thread join what is left.
    if (isServerThread(this)) return;

    // Anywhere else, wait for this client's threads before returning.
    // Detaching them instead — which is what this did — let stop() return,
    // and therefore ~TcpServer() finish, while those threads were still
    // reading members of the object being destroyed. The writer drains what
    // is left of the queue first: every queued id still completes exactly
    // once.
    std::thread reader, writer;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        auto readerIt = clientThreads_.find(clientId);
        if (readerIt != clientThreads_.end()) {
            reader = std::move(readerIt->second);
            clientThreads_.erase(readerIt);
        }
        auto writerIt = clientWriters_.find(clientId);
        if (writerIt != clientWriters_.end()) {
            writer = std::move(writerIt->second);
            clientWriters_.erase(writerIt);
        }
    }

    // Outside clientsMutex_: the threads being joined take that same lock on
    // their way out, so holding it here would deadlock the pair.
    if (writer.joinable()) writer.join();
    if (reader.joinable()) reader.join();
}

void TcpServer::disconnectAllClients() {
    // On one of the server's own threads: disconnect, join nothing (see
    // disconnectClient())
    if (isServerThread(this)) {
        shutAllClients();
        return;
    }

    std::vector<int> ids;
    {
        std::shared_lock<std::shared_mutex> lock(clientsMutex_);
        for (const auto& pair : clients_) {
            ids.push_back(pair.first);
        }
    }

    for (int id : ids) {
        disconnectClient(id);
    }

    // Then the threads of clients that were already gone from the registry
    // (disconnected on one of the server's threads, or by the accept loop),
    // which nothing above claimed. A client that connected after the list
    // above was taken is still registered, and its threads are left alone.
    joinClientThreads();
}

void TcpServer::shutAllClients() {
    std::vector<std::shared_ptr<internal::TcpSendChannel>> channels;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        for (const auto& pair : clients_) channels.push_back(pair.second.channel_);
        clients_.clear();
    }
    // Outside clientsMutex_, like removeClient(). The threads stay in
    // clientThreads_ and clientWriters_ for whoever joins them.
    for (const auto& ch : channels) shutChannel(ch);
}

bool TcpServer::removeClient(int clientId) {
    std::shared_ptr<internal::TcpSendChannel> ch;
    {
        std::unique_lock<std::shared_mutex> lock(clientsMutex_);
        auto it = clients_.find(clientId);
        if (it == clients_.end()) return false;
        ch = it->second.channel_;
        clients_.erase(it);
    }
    // Outside clientsMutex_, like shutAllClients(). Nothing is joined here:
    // this runs on the server's own threads (see isServerThread()), and
    // disconnectClient() joins on any other.
    shutChannel(ch);
    return true;
}

int TcpServer::getClientCount() const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex_);
    return static_cast<int>(clients_.size());
}

std::vector<int> TcpServer::getClientIds() const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex_);
    std::vector<int> ids;
    for (const auto& pair : clients_) {
        ids.push_back(pair.first);
    }
    return ids;
}

const TcpServerClient* TcpServer::getClient(int clientId) const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        return &it->second;
    }
    return nullptr;
}

// =============================================================================
// Send channel plumbing
// =============================================================================
static bool isWouldBlock(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAETIMEDOUT;
#else
    return err == EWOULDBLOCK || err == EAGAIN;
#endif
}

std::shared_ptr<internal::TcpSendChannel> TcpServer::findChannel(int clientId) const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it == clients_.end()) return nullptr;
    return it->second.channel_;
}

std::vector<std::pair<int, std::shared_ptr<internal::TcpSendChannel>>>
TcpServer::snapshotChannels() const {
    std::shared_lock<std::shared_mutex> lock(clientsMutex_);
    std::vector<std::pair<int, std::shared_ptr<internal::TcpSendChannel>>> out;
    out.reserve(clients_.size());
    for (const auto& pair : clients_) out.emplace_back(pair.first, pair.second.channel_);
    return out;
}

bool TcpServer::shutChannel(const std::shared_ptr<internal::TcpSendChannel>& ch) {
    if (!ch) return false;
    // Order matters: refuse new sends, wake everything parked on the channel,
    // then shut the socket down so a send already blocked inside the kernel
    // returns. The descriptor itself is closed by the writer thread on its way
    // out, never here, so this cannot pull it out from under a send in flight.
    if (!ch->open.exchange(false)) return false;

    // The shutdown goes under the mutex because the writer closes the
    // descriptor under it too: without that, a writer that woke, found the
    // queue empty and exited could have closed — and the kernel recycled —
    // this descriptor between the read and the call.
    //
    // The same lock is what makes the notify land: a waiter that has checked
    // its predicate but not yet parked holds the mutex, so this blocks until it
    // is genuinely waiting.
    {
        std::lock_guard<std::mutex> lock(ch->mutex);
        if (ch->socket != INVALID_SOCKET) {
#ifdef _WIN32
            ::shutdown(ch->socket, SD_BOTH);
#else
            ::shutdown(ch->socket, SHUT_RDWR);
#endif
        }
    }
    ch->queued.notify_all();
    ch->room.notify_all();
    return true;
}

// How one payload gets written. Called only by that client's writer thread, and
// never under the channel mutex: a peer that stops draining must not stall the
// next sendAsync().
namespace {

struct WriteOutcome {
    const char* failure = nullptr;   // null = the whole payload was written
    int code = 0;
    size_t written = 0;
    bool truncated = false;          // partial payload written: the stream is unusable

    // What the caller is told. The question a listener actually has is whether
    // the connection is still usable: a timeout that wrote nothing leaves it
    // up, so it is not a disconnect and must not report as one.
    SendError error = SendError::Disconnected;
};

} // namespace

static WriteOutcome writePayload(internal::TcpSendChannel& ch, const void* data, size_t size,
                                 float timeout) {
    WriteOutcome out;
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = size;

    // The timeout measures SILENCE, not the length of the send: a peer that
    // keeps draining is healthy however long the payload takes, and a peer that
    // has stopped reading is what the timeout is for. Measuring total elapsed
    // time instead would drop a healthy client for the offence of being on a
    // slow link with a big payload.
    auto lastProgress = std::chrono::steady_clock::now();

    while (remaining > 0) {
        // Re-read every iteration: shutChannel() clears this to cut a parked
        // send short, and that is the only thing that can.
        if (!ch.open) {
            out.failure = "Client disconnected";
            break;
        }

        int sent = static_cast<int>(::send(ch.socket, ptr, remaining, TC_SEND_FLAGS));
        if (sent > 0) {
            ptr += sent;
            remaining -= static_cast<size_t>(sent);
            out.written += static_cast<size_t>(sent);
            lastProgress = std::chrono::steady_clock::now();
            continue;
        }
        if (sent == 0) {
            out.failure = "Send failed";
            break;
        }

        int err = SOCKET_ERROR_CODE;
#ifndef _WIN32
        if (err == EINTR) continue;
#endif
        if (!isWouldBlock(err)) {
            out.failure = "Send failed";
            out.code = err;
            break;
        }

        // The peer is not draining its window. Park for one slice so a
        // disconnect can interrupt this, then re-check the deadline.
        if (waitReady(ch.socket, true, kWaitSliceMs) < 0) {
            out.failure = "Send failed";
            out.code = socketError(ch.socket);
            break;
        }
        if (timeout <= 0.0f) continue;

        const std::chrono::duration<float> idle = std::chrono::steady_clock::now() - lastProgress;
        if (idle.count() < timeout) continue;

        out.code = err;
        // A timeout that wrote nothing leaves the stream intact. One that fires
        // mid-payload leaves it truncated with the connection still open, so
        // the next send would splice a fresh message onto a partial one: the
        // client has to go.
        if (out.written > 0) {
            // The client is about to be dropped, so it really has gone.
            out.failure = "Send timed out mid-payload; client dropped";
            out.truncated = true;
        } else {
            out.failure = "Send timed out";
            out.error = SendError::Timeout;
        }
        break;
    }

    return out;
}

// =============================================================================
// Send queue
// =============================================================================

// True when the caller is the writer thread for this channel — i.e. it is
// inside an onSendComplete or onError listener firing from that thread.
// Waiting for a queued item from there would wait on the very thread that has
// to write it.
static bool isOwnWriterThread(const std::shared_ptr<internal::TcpSendChannel>& ch) {
    std::lock_guard<std::mutex> lock(ch->mutex);
    return ch->writerId == std::this_thread::get_id();
}

SendResult TcpServer::enqueue(const std::shared_ptr<internal::TcpSendChannel>& ch, int clientId,
                              internal::TcpSendItem&& item) {
    (void)clientId;
    const size_t limit = sendAsyncBufferSize_.load();
    std::unique_lock<std::mutex> lock(ch->mutex);

    if (!ch->open || ch->dropped) return SendResult{SendError::Disconnected, 0};

    if (limit > 0 && ch->pendingBytes >= limit) {
        // A high-water mark, not a cap. The queue is only refused once it
        // ALREADY holds the limit, so a single message of any size still goes
        // through on an empty queue: there is no separate "too large" case, and
        // the queue overshoots by at most one message.
        if (!item.waiter) return SendResult{SendError::QueueFull, 0};

        // A synchronous send waits for room rather than failing, which is why
        // QueueFull is something only sendAsync() can return. The mark is
        // re-read here rather than captured: raising it while a send waits
        // should let that send through at the next completion, not leave it
        // parked against the old value.
        ch->room.wait(lock, [&] {
            const size_t mark = sendAsyncBufferSize_.load();
            return !ch->open || ch->dropped || mark == 0 || ch->pendingBytes < mark;
        });
        if (!ch->open || ch->dropped) return SendResult{SendError::Disconnected, 0};
    }

    const uint64_t id = ++nextSendId_;
    item.id = id;
    ch->pendingBytes += item.size;
    ch->queue.push_back(std::move(item));
    lock.unlock();

    ch->queued.notify_one();
    return SendResult{SendError::None, id};
}

void TcpServer::completeSend(int clientId, const std::shared_ptr<internal::TcpSendChannel>& ch,
                             const internal::TcpSendItem& item, SendError error, size_t bytesSent) {
    {
        std::lock_guard<std::mutex> lock(ch->mutex);
        ch->pendingBytes -= item.size;
    }
    ch->room.notify_all();

    // Wake a synchronous sender before running listeners: it has nothing to
    // learn from them, and no reason to stay parked through one.
    if (item.waiter) {
        {
            std::lock_guard<std::mutex> lock(item.waiter->mutex);
            item.waiter->error = error;
            item.waiter->bytesSent = bytesSent;
            item.waiter->done = true;
        }
        item.waiter->cv.notify_all();
    }

    TcpSendCompleteEventArgs args;
    args.clientId = clientId;
    args.sendId = item.id;
    args.error = error;
    args.bytesSent = bytesSent;
    onSendComplete.notify(args);
}

// =============================================================================
// Client writer thread
//
// One per client, draining that client's queue. Every id it takes off the queue
// completes exactly once, whether it was written, refused or caught by a
// teardown — so a caller counting completions never has to wonder which.
// =============================================================================
void TcpServer::writerThreadFunc(int clientId, std::shared_ptr<internal::TcpSendChannel> ch) {
    tlClientThreadOf = this;

    // Like the receive thread: however this thread ends, it hands its id to
    // the accept loop, which joins it (reapClientThreads). Declared first so
    // it runs last, after the descriptor is closed. Safe for the same reason:
    // this thread is never detached, so whoever tears the server down joins
    // it first.
    struct FinishedMark {
        TcpServer* server;
        int id;
        ~FinishedMark() {
            std::unique_lock<std::shared_mutex> lock(server->clientsMutex_);
            server->finishedWriters_.push_back(id);
        }
    } finishedMark{this, clientId};
    {
        std::lock_guard<std::mutex> lock(ch->mutex);
        ch->writerId = std::this_thread::get_id();
    }

    for (;;) {
        internal::TcpSendItem item;
        {
            std::unique_lock<std::mutex> lock(ch->mutex);
            ch->queued.wait(lock, [&] { return !ch->queue.empty() || !ch->open; });
            if (ch->queue.empty()) break;   // closed and drained
            item = std::move(ch->queue.front());
            ch->queue.pop_front();
        }

        // Closed, or already given up on: report the id and move on rather than
        // writing to a socket that is going away.
        if (!ch->open || ch->dropped) {
            completeSend(clientId, ch, item, SendError::Disconnected, 0);
            continue;
        }

        const WriteOutcome out = writePayload(*ch, item.data(), item.size, sendTimeout_.load());
        if (!out.failure) {
            completeSend(clientId, ch, item, SendError::None, out.written);
            continue;
        }

        if (out.truncated) {
            // The stream is unusable, so the client has to go. Mark the channel
            // and shut the socket down; the receive thread wakes and does the
            // removal.
            ch->dropped = true;
            std::lock_guard<std::mutex> lock(ch->mutex);
            if (ch->socket != INVALID_SOCKET) {
#ifdef _WIN32
                ::shutdown(ch->socket, SD_BOTH);
#else
                ::shutdown(ch->socket, SHUT_RDWR);
#endif
            }
        }
        notifyError(out.failure, out.code, clientId);
        completeSend(clientId, ch, item, out.error, out.written);
    }

    // This thread is the only one that writes to the descriptor, so it is the
    // one that closes it. Teardown joins it first and therefore can never pull
    // the descriptor out from under a send in flight — nor hand it to a fresh
    // connection while this loop still holds it.
    std::lock_guard<std::mutex> lock(ch->mutex);
    if (ch->socket != INVALID_SOCKET) {
        CLOSE_SOCKET(ch->socket);
        ch->socket = INVALID_SOCKET;
    }
}

// =============================================================================
// Data send
// =============================================================================

// Blocking: returns once the whole payload has been handed to the kernel, or on
// error. It queues the payload like sendAsync() does and waits for that one id,
// so sync and async sends to the same client stay in order and share one code
// path and one idle timeout. The payload is lent, not copied: the caller is
// parked here until the item is done with it.
bool TcpServer::send(int clientId, const void* data, size_t size) {
    std::shared_ptr<internal::TcpSendChannel> ch = findChannel(clientId);
    if (!ch) {
        notifyError("Client not found", 0, clientId);
        return false;
    }

    if (isOwnWriterThread(ch)) {
        // A listener firing on this client's writer thread cannot wait for that
        // same thread to write anything. sendAsync() from there is fine.
        notifyError("send() cannot wait inside a listener on this client's send thread; "
                    "use sendAsync()", 0, clientId);
        return false;
    }

    internal::TcpSendItem item;
    item.borrowed = data;
    item.size = size;
    item.waiter = std::make_shared<internal::TcpSendWaiter>();
    std::shared_ptr<internal::TcpSendWaiter> waiter = item.waiter;

    const SendResult queuedResult = enqueue(ch, clientId, std::move(item));
    if (!queuedResult) {
        // Write failures are reported by the writer thread; this is the one
        // case the caller has to report itself.
        notifyError("Client disconnected", 0, clientId);
        return false;
    }

    std::unique_lock<std::mutex> lock(waiter->mutex);
    waiter->cv.wait(lock, [&] { return waiter->done; });
    return waiter->error == SendError::None;
}

bool TcpServer::send(int clientId, const std::vector<char>& data) {
    return send(clientId, data.data(), data.size());
}

bool TcpServer::send(int clientId, const std::string& message) {
    return send(clientId, message.data(), message.size());
}

SendResult TcpServer::sendAsync(int clientId, const void* data, size_t size) {
    if (!running_) return SendResult{SendError::NotRunning, 0};

    std::shared_ptr<internal::TcpSendChannel> ch = findChannel(clientId);
    if (!ch) {
        notifyError("Client not found", 0, clientId);
        return SendResult{SendError::ClientNotFound, 0};
    }

    internal::TcpSendItem item;
    item.owned = std::make_shared<const std::vector<char>>(
        static_cast<const char*>(data), static_cast<const char*>(data) + size);
    item.size = size;

    const SendResult result = enqueue(ch, clientId, std::move(item));
    if (!result) notifyError(result.error == SendError::QueueFull ? "Send queue full"
                                                                 : "Client disconnected",
                             0, clientId);
    return result;
}

SendResult TcpServer::sendAsync(int clientId, std::vector<char>&& data) {
    if (!running_) return SendResult{SendError::NotRunning, 0};

    std::shared_ptr<internal::TcpSendChannel> ch = findChannel(clientId);
    if (!ch) {
        notifyError("Client not found", 0, clientId);
        return SendResult{SendError::ClientNotFound, 0};
    }

    internal::TcpSendItem item;
    item.size = data.size();
    item.owned = std::make_shared<const std::vector<char>>(std::move(data));

    const SendResult result = enqueue(ch, clientId, std::move(item));
    if (!result) notifyError(result.error == SendError::QueueFull ? "Send queue full"
                                                                 : "Client disconnected",
                             0, clientId);
    return result;
}

SendResult TcpServer::sendAsync(int clientId, const std::string& message) {
    return sendAsync(clientId, message.data(), message.size());
}

void TcpServer::broadcast(const void* data, size_t size) {
    // Queue to every client first, then wait for all of them. Sending to each
    // in turn would put a slow peer in front of the fast ones behind it; the
    // call still returns only once everyone has the payload (or has failed).
    struct Pending {
        int clientId;
        std::shared_ptr<internal::TcpSendWaiter> waiter;
    };
    std::vector<Pending> pending;

    // One pass over the registry, not one lookup per client: a broadcast from
    // a draw loop used to take the client lock once for the id list and again
    // for every id in it. A client that goes away mid-broadcast is reported by
    // its own channel (closed) rather than by a failed lookup.
    for (auto& [id, ch] : snapshotChannels()) {
        if (isOwnWriterThread(ch)) continue;   // see send()

        internal::TcpSendItem item;
        item.borrowed = data;   // every waiter is joined below, so `data` outlives them
        item.size = size;
        item.waiter = std::make_shared<internal::TcpSendWaiter>();
        std::shared_ptr<internal::TcpSendWaiter> waiter = item.waiter;

        if (!enqueue(ch, id, std::move(item))) {
            notifyError("Client disconnected", 0, id);
            continue;
        }
        pending.push_back({id, std::move(waiter)});
    }

    for (auto& p : pending) {
        std::unique_lock<std::mutex> lock(p.waiter->mutex);
        p.waiter->cv.wait(lock, [&] { return p.waiter->done; });
    }
}

void TcpServer::broadcast(const std::vector<char>& data) {
    broadcast(data.data(), data.size());
}

void TcpServer::broadcast(const std::string& message) {
    broadcast(message.data(), message.size());
}

int TcpServer::broadcastAsync(const void* data, size_t size) {
    if (!running_) return 0;

    // Buffered once and shared, not copied per client: a broadcast of a frame
    // to twenty peers should not mean twenty copies of the frame.
    auto payload = std::make_shared<const std::vector<char>>(
        static_cast<const char*>(data), static_cast<const char*>(data) + size);

    int queued = 0;
    for (auto& [id, ch] : snapshotChannels()) {
        internal::TcpSendItem item;
        item.owned = payload;
        item.size = size;
        if (enqueue(ch, id, std::move(item))) ++queued;
    }
    return queued;
}

int TcpServer::broadcastAsync(const std::vector<char>& data) {
    return broadcastAsync(data.data(), data.size());
}

int TcpServer::broadcastAsync(const std::string& message) {
    return broadcastAsync(message.data(), message.size());
}

size_t TcpServer::getSendAsyncPendingBytes(int clientId) const {
    std::shared_ptr<internal::TcpSendChannel> ch = findChannel(clientId);
    if (!ch) return 0;
    std::lock_guard<std::mutex> lock(ch->mutex);
    return ch->pendingBytes;
}

// =============================================================================
// Settings
// =============================================================================
void TcpServer::setSendTimeout(float seconds) {
    sendTimeout_ = seconds < 0.0f ? 0.0f : seconds;
}

void TcpServer::setReceiveBufferSize(size_t size) {
    receiveBufferSize_ = size;
}

void TcpServer::setSendAsyncBufferSize(size_t bytes) {
    sendAsyncBufferSize_ = bytes;
}

size_t TcpServer::getSendAsyncBufferSize() const {
    return sendAsyncBufferSize_.load();
}

// =============================================================================
// Information retrieval
// =============================================================================
int TcpServer::getPort() const {
    return port_;
}

// =============================================================================
// Error notification
// =============================================================================
void TcpServer::notifyError(const std::string& msg, int code, int clientId) {
    TcpServerErrorEventArgs args;
    args.message = msg;
    args.errorCode = code;
    args.clientId = clientId;
    onError.notify(args);
}

} // namespace trussc
