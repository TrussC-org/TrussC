// =============================================================================
// tcSendResult.h - result and completion types for asynchronous sends
// =============================================================================
#pragma once
#include "tc/utils/tcAnnotations.h"

// =============================================================================
// The types an asynchronous send speaks in. They live in their own header
// because TcpServer::sendAsync() and TcpClient::sendAsync() report through the
// same vocabulary; neither has to include the other to use it.
//
// SendResult follows LoadResult (tcLoadResult.h): truthy on success, `error`
// says why otherwise. It carries an id as well, because "did this get queued"
// and "which send was that" are both questions the caller has.
// =============================================================================

#include <cstddef>
#include <cstdint>

namespace trussc {

// Why a send could not be queued, or how a queued one finished.
// None == success.
enum class SendError {
    None = 0,        // queued (SendResult) / the whole payload reached the kernel (completion)
    ClientNotFound,  // no client is registered under that id
    Disconnected,    // the connection went away before the payload was written
    QueueFull,       // the send queue is already at its high-water mark
    NotRunning,      // the server (or client) is not connected
};

// Result of sendAsync(). Truthy when the payload was queued.
struct SendResult {
    SendError error = SendError::None;
    uint64_t id = 0;    // non-zero once queued; echoed back by onSendComplete

    bool ok() const { return error == SendError::None; }
    explicit operator bool() const { return ok(); }
};

// A queued send finished. Fires exactly once for every non-zero SendResult::id,
// including sends still in the queue when the connection goes away.
struct TcpSendCompleteEventArgs {
    int clientId = -1;                    // -1 when the sender has no clients (TcpClient)
    uint64_t sendId = 0;                  // matches the SendResult that queued it
    SendError error = SendError::None;    // None = the whole payload reached the kernel
    size_t bytesSent = 0;                 // how much of it got through
};

// Short label for a SendError value ("QueueFull", ...). For log messages.
inline const char* sendErrorName(SendError e) {
    switch (e) {
        case SendError::None:           return "None";
        case SendError::ClientNotFound: return "ClientNotFound";
        case SendError::Disconnected:   return "Disconnected";
        case SendError::QueueFull:      return "QueueFull";
        case SendError::NotRunning:     return "NotRunning";
    }
    return "Unknown";
}

} // namespace trussc

namespace tc = trussc;
