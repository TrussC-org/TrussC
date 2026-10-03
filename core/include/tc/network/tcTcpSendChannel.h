// Shared TCP client/server send queue (internal, not public API).
#pragma once
#include "tc/network/tcSendResult.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#endif
namespace trussc::internal {
// -----------------------------------------------------------------------------
// A synchronous send waiting on its queued payload.
//
// send() is sendAsync() plus this: it queues the payload like anything else and
// blocks here until the writer thread reports back. Sharing one queue is what
// keeps sync and async sends to the same client in order, on one code path,
// under one idle timeout.
// -----------------------------------------------------------------------------
struct TcpSendWaiter {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    SendError error = SendError::None;
    size_t bytesSent = 0;
};

// -----------------------------------------------------------------------------
// One queued payload.
//
// sendAsync() has to own its bytes, since it returns before they are written,
// and broadcastAsync() hands the same buffer to every client rather than
// copying a frame once per peer. send() blocks until the item completes, so its
// caller's buffer is still there: it lends the pointer instead of copying, and
// stays as cheap as it was before the queue existed.
// -----------------------------------------------------------------------------
struct TcpSendItem {
    std::shared_ptr<const std::vector<char>> owned;   // sendAsync / broadcastAsync
    const void* borrowed = nullptr;                   // send(), whose caller is blocked
    size_t size = 0;
    uint64_t id = 0;
    std::shared_ptr<TcpSendWaiter> waiter;            // set by send(), null otherwise

    const void* data() const { return owned ? static_cast<const void*>(owned->data()) : borrowed; }
};

// -----------------------------------------------------------------------------
// Per-client send channel.
//
// Sending must NOT hold the server-wide client mutex: a client that stops
// reading would otherwise block client registration, disconnects and every
// other send (head-of-line blocking). Each client therefore owns its own queue,
// held by shared_ptr so an in-flight send keeps the channel alive even after
// the client is erased from the map.
//
// One writer drains each queue. TcpServer's writer owns and closes the channel
// descriptor; TcpClient instead joins its writer before closing its socket
// under its I/O mutex. Both keep teardown from pulling a descriptor out from
// under a send. `mutex` guards the queue (and the server descriptor), never the
// send itself: a slow peer must not stall the next sendAsync().
//
// The socket is non-blocking and the send loop waits in short slices, so
// clearing `open` is what cuts a send short when the client goes away: nothing
// else can interrupt a send that is already parked (Winsock's shutdown() does
// not). It is cleared before the socket is shut down, so a send that wakes up
// mid-teardown fails instead of writing to a closed (or recycled) descriptor.
// -----------------------------------------------------------------------------
struct TcpSendChannel {
#ifdef _WIN32
    SOCKET socket = INVALID_SOCKET;
#else
    int socket = -1;
#endif
    std::mutex mutex;
    std::condition_variable queued;   // writer waits here for work, or for the channel to close
    std::condition_variable room;     // send() waits here when the queue is at its mark
    std::deque<TcpSendItem> queue;
    size_t pendingBytes = 0;          // accepted and not yet completed
    std::thread::id writerId;         // set once by the writer; send() must not wait on itself
    std::atomic<bool> open{true};

    // The send path gave up on this client (a timeout truncated a payload).
    // The receive thread does the removal.
    std::atomic<bool> dropped{false};
};

} // namespace trussc::internal
