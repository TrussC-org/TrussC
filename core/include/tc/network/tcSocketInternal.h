// =============================================================================
// tcSocketInternal.h - helpers shared by the socket classes (not public API)
//
// Included by the .cpp files of TcpClient, TcpServer, UdpSocket and the
// tcxTls TlsClient only. Nothing here holds state: the process-wide Winsock
// initialisation lives in tcSocketInternal.cpp and is reached through
// ensureWinsock().
// =============================================================================
#pragma once

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <sys/socket.h>
#endif

// Writing to a socket the peer already closed raises SIGPIPE, whose default
// action terminates the process. MSG_NOSIGNAL suppresses it per call, and both
// Linux and current Apple SDKs define it. Older Apple SDKs do not, so every
// socket also gets SO_NOSIGPIPE through setNoSigpipe() — either mechanism alone
// is enough (verified on macOS 26.5 with a four-way probe: unprotected sends die
// on signal 13, each option alone survives). Windows has no SIGPIPE at all and
// does not define MSG_NOSIGNAL, so the flag is 0 there.
#if defined(MSG_NOSIGNAL)
    #define TC_SEND_FLAGS MSG_NOSIGNAL
#else
    #define TC_SEND_FLAGS 0
#endif

namespace trussc {
namespace internal {

// Start Winsock for the whole process, once, on first use. It is never torn
// down: WSACleanup() is not called, and the OS reclaims Winsock at exit. A
// per-class count that called WSACleanup() whenever it reached zero unbalanced
// Winsock's process-wide reference count on every later 0 -> 1 -> 0 cycle, and
// once that count ran out every socket in the process stopped working.
// Returns false if WSAStartup() failed. Always true on other platforms.
bool ensureWinsock();

// Belt and braces for Apple SDKs that predate MSG_NOSIGNAL (see TC_SEND_FLAGS).
// A no-op where SO_NOSIGPIPE does not exist.
#ifdef _WIN32
inline void setNoSigpipe(SOCKET) {}
#else
inline void setNoSigpipe(int s) {
#ifdef SO_NOSIGPIPE
    int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)s;
#endif
}
#endif

} // namespace internal
} // namespace trussc
