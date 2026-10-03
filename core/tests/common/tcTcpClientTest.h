#pragma once
#include <TrussC.h>
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

struct RawListener {
    TestSocket fd = INVALID_SOCKET;
    int port = 0;
    explicit RawListener(int backlog = 8, bool smallWindow = false) {
        fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (smallWindow) {
            int bytes = 4096;
            setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bytes), sizeof(bytes));
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
        return ::accept(fd, nullptr, nullptr);
    }
};

struct ProbeClient : TcpClient {
    atomic<int> attempts{0};
    bool connect(const string& host, int port) override {
        ++attempts;
        return TcpClient::connect(host, port);
    }
    bool pending() const { return connectPending_; }
    void smallSendBuffer() {
        lock_guard<mutex> held(socketMutex_);
        int size = 4096;
        setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&size), sizeof(size));
    }
};
} // namespace
