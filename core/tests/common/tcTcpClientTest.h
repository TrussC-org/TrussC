#pragma once
#include <TrussC.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace {
using namespace std;
using namespace tc;
#ifdef _WIN32
using TestSocket = SOCKET;
#else
using TestSocket = int;
#endif

atomic<int> failures{0};
void check(const char* label, bool ok) {
    printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

template<class Predicate> bool until(Predicate ready) {
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(10);
    while (!ready()) {
        if (chrono::steady_clock::now() >= deadline) return ready();
        this_thread::sleep_for(chrono::milliseconds(2));
    }
    return true;
}

// A harness deadline catches hangs. No assertion compares elapsed times.
struct Watchdog {
    mutex lock;
    condition_variable cv;
    bool done = false;
    thread worker;
    Watchdog() : worker([this] {
        unique_lock<mutex> held(lock);
        if (!cv.wait_for(held, chrono::seconds(60), [this] { return done; })) {
            fprintf(stderr, "FAIL: client scenario did not finish\n");
            fflush(nullptr);
            _Exit(1);
        }
    }) {}
    ~Watchdog() {
        { lock_guard<mutex> held(lock); done = true; }
        cv.notify_one();
        worker.join();
    }
};

int socketBuffer(TestSocket fd, int option) {
    int bytes = 0;
#ifdef _WIN32
    int len = sizeof(bytes);
#else
    socklen_t len = sizeof(bytes);
#endif
    if (getsockopt(fd, SOL_SOCKET, option, reinterpret_cast<char*>(&bytes), &len) != 0 || bytes <= 0) {
        check("read socket buffer size", false);
        _Exit(1);
    }
    return bytes;
}

void smallSocketBuffer(TestSocket fd, int option) {
    const int bytes = 4096;
    if (setsockopt(fd, SOL_SOCKET, option, reinterpret_cast<const char*>(&bytes), sizeof(bytes)) != 0) {
        fprintf(stderr, "setsockopt failed: %d\n", SOCKET_ERROR_CODE);
        check("set small socket buffer", false);
        _Exit(1);
    }
    // Linux doubles the requested size; don't assume exact readback equality.
    printf("%s: requested %d, actual %d bytes\n",
           option == SO_RCVBUF ? "SO_RCVBUF" : "SO_SNDBUF", bytes, socketBuffer(fd, option));
}

struct RawListener {
    TestSocket fd = INVALID_SOCKET;
    int port = 0;
    bool smallWindow;
    explicit RawListener(int backlog = 8, bool smallWindow = false) : smallWindow(smallWindow) {
        fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (smallWindow) {
            // Set before listen/connection establishment: shrinking a receive
            // window after accept is not reliable on Windows.
            smallSocketBuffer(fd, SO_RCVBUF);
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifdef _WIN32
        int len = sizeof(addr);
#else
        socklen_t len = sizeof(addr);
#endif
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0 || ::listen(fd, backlog) != 0) {
            check("raw listener setup", false);
            _Exit(1);
        }
        port = ntohs(addr.sin_port);
    }
    ~RawListener() { CLOSE_SOCKET(fd); }
    TestSocket acceptPeer() {
#ifdef _WIN32
        fd_set ready;
        FD_ZERO(&ready); FD_SET(fd, &ready);
        timeval timeout{10, 0};
        if (select(0, &ready, nullptr, nullptr, &timeout) <= 0) return INVALID_SOCKET;
#else
        pollfd ready{fd, POLLIN, 0};
        if (poll(&ready, 1, 10000) <= 0) return INVALID_SOCKET;
#endif
        TestSocket peer = ::accept(fd, nullptr, nullptr);
        if (smallWindow && peer != INVALID_SOCKET)
            printf("accepted SO_RCVBUF: %d bytes\n", socketBuffer(peer, SO_RCVBUF));
        return peer;
    }
};

struct ProbeClient : TcpClient {
    atomic<int> attempts{0};
    bool connect(const string& host, int port) override {
        ++attempts;
        return TcpClient::connect(host, port);
    }
    bool pending() const { return connectPending_; }
};

// Real socket back-pressure, with a bounded amount offered per native send.
// Winsock may accept an oversized request in full even with a small SO_SNDBUF;
// a single 64 MiB request therefore need not exercise the idle deadline at all.
// Keep the large queued payload, but fill the non-reading peer in small steps.
// Only the request length changes: the product performs send/error mapping,
// readiness waits, progress accounting, timeout and cancellation unchanged.
struct BackpressureClient : ProbeClient {
    atomic<bool> wouldBlock{false};
    atomic<size_t> acceptedBytes{0};
    void smallSendBuffer() {
        lock_guard<mutex> held(socketMutex_);
        smallSocketBuffer(socket_, SO_SNDBUF);
    }
    int writeSendStep(const void* data, size_t size, bool& forWrite, int& error) override {
        int sent = TcpClient::writeSendStep(data, std::min(size, size_t(4096)), forWrite, error);
        if (sent > 0) acceptedBytes += static_cast<size_t>(sent);
#ifdef _WIN32
        const bool blocked = error == WSAEWOULDBLOCK;
#else
        const bool blocked = error == EWOULDBLOCK || error == EAGAIN;
#endif
        if (sent == -1 && blocked) wouldBlock = true;
        return sent;
    }
};
} // namespace
