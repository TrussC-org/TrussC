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

} // namespace internal
} // namespace trussc
