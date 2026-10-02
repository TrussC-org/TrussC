// =============================================================================
// tcKeptThreads.h - threads a network client could not join yet (not public
// API)
//
// A client cannot join the thread it runs on. When disconnect(), connect()
// (or, for UdpSocket, stopReceiving() / close()) runs on one of the client's
// own threads, from an inline listener, that thread used to be detached and
// nothing waited for it: it went on in the listener, in Event::notify() and in
// its loop after the client had moved on, or had been destroyed (#543). The
// client keeps such a thread here instead, and joins it from another thread:
// in the next connect() or disconnect() (startReceiving(), stopReceiving() or
// close() for UdpSocket), or in the destructor.
//
// Used by TcpClient, the tcxTls TlsClient and UdpSocket.
// =============================================================================
#pragma once

#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace trussc {
namespace internal {

class KeptThreads {
public:
    KeptThreads() = default;
    KeptThreads(const KeptThreads&) = delete;
    KeptThreads& operator=(const KeptThreads&) = delete;

    // The owner's destructor. joinOthers() has already run in it (the
    // client's disconnect work), so what is left here is the calling thread
    // at most: a client destroyed on one of its own threads (in a listener).
    // That thread cannot join itself and is detached, as before: it returns
    // into the destroyed client once the listener returns, so the client
    // must not read itself on it then (TcpClient's and TlsClient's receive
    // threads check their alive_ token, #262; the client classes document
    // the rest). Anything else still kept is joined.
    ~KeptThreads() {
        joinOthers();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& t : threads_) t.detach();
        threads_.clear();
    }

    // Join t. On t itself, which cannot join itself, keep it instead: a
    // later joinOthers() on another thread joins it.
    void release(std::thread& t) {
        if (!t.joinable()) return;
        if (t.get_id() == std::this_thread::get_id()) {
            std::lock_guard<std::mutex> lock(mutex_);
            threads_.push_back(std::move(t));
        } else {
            t.join();
        }
    }

    // Join every kept thread but the calling one, which stays kept. A thread
    // kept here has been told to stop (its client cleared the flags, or a
    // newer generation took over), so this waits only for the rest of its
    // listener and its way out.
    void joinOthers() {
        std::vector<std::thread> others;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (threads_.empty()) return;
            const std::thread::id self = std::this_thread::get_id();
            std::vector<std::thread> stay;
            for (auto& t : threads_) {
                if (t.get_id() == self) stay.push_back(std::move(t));
                else others.push_back(std::move(t));
            }
            threads_ = std::move(stay);
        }
        for (auto& t : others) t.join();
    }

private:
    std::mutex mutex_;
    std::vector<std::thread> threads_;
};

} // namespace internal
} // namespace trussc
