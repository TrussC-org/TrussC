// =============================================================================
// tcSocketInternal.cpp - process-wide socket setup shared by the socket classes
// =============================================================================

#include "tc/network/tcSocketInternal.h"
#include "tc/utils/tcLog.h"

#include <mutex>
#include <utility>
#ifndef _WIN32
#include <arpa/inet.h>
#include <cerrno>
#endif

namespace trussc {
namespace internal {

namespace {
std::string invalidBindAddress(const std::string& address,
                               const char* reason = "Invalid bind address") {
    std::string message = std::string(reason) + " \"";
    constexpr size_t previewLimit = 32;
    // Bound (configuration and diagnostic storage): retain only a valid IPv4 literal (15 bytes) or an error with 32 input bytes escaped to at most four characters each.
    for (size_t i = 0; i < address.size() && i < previewLimit; ++i) {
        const auto byte = static_cast<unsigned char>(address[i]);
        if (byte == '"' || byte == '\\') {
            message += '\\';
            message += static_cast<char>(byte);
        } else if (byte < 0x20 || byte >= 0x7f) {
            constexpr char hex[] = "0123456789abcdef";
            message += "\\x";
            message += hex[byte >> 4];
            message += hex[byte & 0xf];
        } else {
            message += static_cast<char>(byte);
        }
    }
    if (address.size() > previewLimit) message += "...";
    return message + "\": use a numeric IPv4 address such as 127.0.0.1 (names are not resolved)";
}
} // namespace

std::string parseBindAddress(const std::string& address, in_addr& parsed) {
    if (address.empty()) {
        parsed.s_addr = htonl(INADDR_ANY);
        return {};
    }
    // Check length before scanning; 45 also allows an IPv6-specific diagnostic.
    if (address.size() > 45) return invalidBindAddress(address);
    if (address.find('\0') != std::string::npos)
        return invalidBindAddress(address);
    if (address.find(':') != std::string::npos)
        return invalidBindAddress(address, "IPv6 is not supported for bind addresses");
    // An IPv4 dotted-quad literal has at most 15 characters.
    if (address.size() > 15 || inet_pton(AF_INET, address.c_str(), &parsed) != 1)
        return invalidBindAddress(address);
    return {};
}

void saveBindAddress(const std::string& input, std::string& address, std::string& error) {
    in_addr parsed{};
    auto diagnostic = parseBindAddress(input, parsed);
    if (diagnostic.empty()) address = input;
    else address.clear();
    error = std::move(diagnostic);
}

bool isBindAddressUnavailable(int code) {
#ifdef _WIN32
    return code == WSAEADDRNOTAVAIL;
#else
    return code == EADDRNOTAVAIL;
#endif
}

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
