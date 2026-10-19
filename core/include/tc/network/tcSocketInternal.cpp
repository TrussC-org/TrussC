// =============================================================================
// tcSocketInternal.cpp - process-wide socket setup shared by the socket classes
// =============================================================================

#include "tc/network/tcSocketInternal.h"
#include "tc/utils/tcLog.h"

#include <mutex>

namespace trussc {
namespace internal {

bool ensureWinsock() {
#ifdef _WIN32
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            logError() << "Winsock initialization failed: " << result;
            return;
        }
        ok = true;
    });
    return ok;
#else
    return true;
#endif
}

} // namespace internal
} // namespace trussc
