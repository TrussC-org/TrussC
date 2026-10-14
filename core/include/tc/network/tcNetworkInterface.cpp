// =============================================================================
// tcNetworkInterface.cpp - Network interface enumeration implementation
// =============================================================================

#include "tc/network/tcNetworkInterface.h"

#include "tc/utils/tcLog.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "ws2_32.lib")
#elif !defined(__EMSCRIPTEN__)
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <net/if.h>
    #include <ifaddrs.h>
    #ifdef __APPLE__
        #include <net/if_dl.h>
    #else
        #include <netpacket/packet.h>
    #endif
#endif

namespace trussc {

// ---------------------------------------------------------------------------
// Local helpers (no socket / no platform dependency)
// ---------------------------------------------------------------------------
namespace {

std::string toLower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}

// Parse a dotted-quad IPv4 string to a host-order uint32. Rejects anything with
// ':' (IPv6) or malformed octets. Hand-rolled to avoid needing Winsock init.
bool parseIPv4(const std::string& s, uint32_t& out) {
    if (s.empty() || s.find(':') != std::string::npos) return false;
    uint32_t parts[4];
    size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        size_t dot = (i < 3) ? s.find('.', start) : s.size();
        if (i < 3 && dot == std::string::npos) return false;
        std::string tok = s.substr(start, dot - start);
        if (tok.empty() || tok.size() > 3) return false;
        uint32_t v = 0;
        for (char ch : tok) {
            if (ch < '0' || ch > '9') return false;
            v = v * 10 + static_cast<uint32_t>(ch - '0');
        }
        if (v > 255) return false;
        parts[i] = v;
        start = dot + 1;
    }
    out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return true;
}

std::string formatIPv4(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                  (v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return buf;
}

std::string prefixToMaskV4(int prefix) {
    uint32_t m = (prefix <= 0) ? 0u
               : (prefix >= 32) ? 0xFFFFFFFFu
               : (0xFFFFFFFFu << (32 - prefix));
    return formatIPv4(m);
}

// Extract the byte values of a MAC string, skipping any separators (':' '-' etc).
std::vector<int> macBytes(const std::string& mac) {
    std::vector<int> bytes;
    int hi = -1;
    for (char c : mac) {
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else continue; // separator
        if (hi < 0) hi = v;
        else { bytes.push_back(hi * 16 + v); hi = -1; }
    }
    return bytes;
}

std::string formatMac(const unsigned char* b, int len) {
    if (len != 6) return "";
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  b[0], b[1], b[2], b[3], b[4], b[5]);
    return buf;
}

} // namespace

// ---------------------------------------------------------------------------
// Address classification helpers
// ---------------------------------------------------------------------------

bool isLoopback(const std::string& addr) {
    uint32_t ip;
    if (parseIPv4(addr, ip)) return ((ip >> 24) & 0xFF) == 127;     // 127.0.0.0/8
    return toLower(addr) == "::1";
}

bool isPrivate(const std::string& addr) {
    uint32_t ip;
    if (!parseIPv4(addr, ip)) return false;
    if ((ip & 0xFF000000u) == 0x0A000000u) return true; // 10.0.0.0/8
    if ((ip & 0xFFF00000u) == 0xAC100000u) return true; // 172.16.0.0/12
    if ((ip & 0xFFFF0000u) == 0xC0A80000u) return true; // 192.168.0.0/16
    return false;
}

bool isLinkLocal(const std::string& addr) {
    uint32_t ip;
    if (parseIPv4(addr, ip)) return (ip & 0xFFFF0000u) == 0xA9FE0000u; // 169.254.0.0/16
    std::string lo = toLower(addr);
    // fe80::/10 spans fe80.. through febf..
    return lo.rfind("fe8", 0) == 0 || lo.rfind("fe9", 0) == 0 ||
           lo.rfind("fea", 0) == 0 || lo.rfind("feb", 0) == 0;
}

bool sameSubnet(const std::string& a, const std::string& b, const std::string& netmask) {
    uint32_t ia, ib, mask;
    if (!parseIPv4(a, ia) || !parseIPv4(b, ib) || !parseIPv4(netmask, mask)) return false;
    return (ia & mask) == (ib & mask);
}

std::string getOui(const std::string& mac) {
    auto bytes = macBytes(mac);
    if (bytes.size() < 3) return "";
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X", bytes[0], bytes[1], bytes[2]);
    return buf;
}

bool isLocallyAdministered(const std::string& mac) {
    auto bytes = macBytes(mac);
    if (bytes.empty()) return false;
    return (bytes[0] & 0x02) != 0;
}

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------

#ifdef _WIN32

std::vector<NetworkInterface> listNetworkInterfaces() {
    std::vector<NetworkInterface> result;

    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG bufLen = 16384;
    std::vector<unsigned char> buf(bufLen);
    DWORD ret = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
                                   reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &bufLen);
        if (ret == ERROR_BUFFER_OVERFLOW) { buf.resize(bufLen); continue; }
        break;
    }
    if (ret != NO_ERROR) {
        logWarning("Network") << "GetAdaptersAddresses failed (" << ret << ")";
        return result;
    }

    char addrBuf[INET6_ADDRSTRLEN];
    for (auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); aa; aa = aa->Next) {
        // Friendly name (wide -> utf8)
        std::string name;
        if (aa->FriendlyName) {
            int n = WideCharToMultiByte(CP_UTF8, 0, aa->FriendlyName, -1, nullptr, 0, nullptr, nullptr);
            if (n > 0) {
                std::vector<char> tmp(n);
                WideCharToMultiByte(CP_UTF8, 0, aa->FriendlyName, -1, tmp.data(), n, nullptr, nullptr);
                name = tmp.data();
            }
        }
        std::string mac = formatMac(aa->PhysicalAddress, static_cast<int>(aa->PhysicalAddressLength));
        bool isUp = (aa->OperStatus == IfOperStatusUp);
        bool isLoop = (aa->IfType == IF_TYPE_SOFTWARE_LOOPBACK);

        for (auto* ua = aa->FirstUnicastAddress; ua; ua = ua->Next) {
            sockaddr* sa = ua->Address.lpSockaddr;
            if (!sa) continue;
            NetworkInterface ni;
            ni.name = name;
            ni.mac = mac;
            ni.isUp = isUp;
            ni.isLoopback = isLoop;
            if (sa->sa_family == AF_INET) {
                auto* sin = reinterpret_cast<sockaddr_in*>(sa);
                if (inet_ntop(AF_INET, &sin->sin_addr, addrBuf, sizeof(addrBuf)))
                    ni.address = addrBuf;
                ni.isIPv4 = true;
                ni.netmask = prefixToMaskV4(ua->OnLinkPrefixLength);
            } else if (sa->sa_family == AF_INET6) {
                auto* sin6 = reinterpret_cast<sockaddr_in6*>(sa);
                if (inet_ntop(AF_INET6, &sin6->sin6_addr, addrBuf, sizeof(addrBuf)))
                    ni.address = addrBuf;
                ni.isIPv4 = false;
            } else {
                continue;
            }
            if (!ni.address.empty()) result.push_back(std::move(ni));
        }
    }
    return result;
}

#elif defined(__EMSCRIPTEN__)

std::vector<NetworkInterface> listNetworkInterfaces() {
    logWarning("Network") << "listNetworkInterfaces() is not available on the web backend";
    return {};
}

#else // POSIX (macOS / Linux)

std::vector<NetworkInterface> listNetworkInterfaces() {
    std::vector<NetworkInterface> result;

    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0 || !ifap) {
        logWarning("Network") << "getifaddrs failed";
        return result;
    }

    // Pass 1: MAC lives on a separate link-level entry per interface name.
    std::map<std::string, std::string> macByName;
    for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || !ifa->ifa_name) continue;
    #ifdef __APPLE__
        if (ifa->ifa_addr->sa_family == AF_LINK) {
            auto* sdl = reinterpret_cast<struct sockaddr_dl*>(ifa->ifa_addr);
            if (sdl->sdl_alen == 6) {
                auto* p = reinterpret_cast<unsigned char*>(LLADDR(sdl));
                macByName[ifa->ifa_name] = formatMac(p, 6);
            }
        }
    #else
        if (ifa->ifa_addr->sa_family == AF_PACKET) {
            auto* sll = reinterpret_cast<struct sockaddr_ll*>(ifa->ifa_addr);
            if (sll->sll_halen == 6)
                macByName[ifa->ifa_name] = formatMac(sll->sll_addr, 6);
        }
    #endif
    }

    // Pass 2: the IP addresses.
    char addrBuf[INET6_ADDRSTRLEN];
    for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || !ifa->ifa_name) continue;
        int family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;

        NetworkInterface ni;
        ni.name = ifa->ifa_name;
        ni.isUp = (ifa->ifa_flags & IFF_UP) != 0;
        ni.isLoopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;
        auto it = macByName.find(ifa->ifa_name);
        if (it != macByName.end()) ni.mac = it->second;

        if (family == AF_INET) {
            auto* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
            if (inet_ntop(AF_INET, &sin->sin_addr, addrBuf, sizeof(addrBuf)))
                ni.address = addrBuf;
            ni.isIPv4 = true;
            if (ifa->ifa_netmask) {
                auto* nm = reinterpret_cast<sockaddr_in*>(ifa->ifa_netmask);
                if (inet_ntop(AF_INET, &nm->sin_addr, addrBuf, sizeof(addrBuf)))
                    ni.netmask = addrBuf;
            }
        } else {
            auto* sin6 = reinterpret_cast<sockaddr_in6*>(ifa->ifa_addr);
            if (inet_ntop(AF_INET6, &sin6->sin6_addr, addrBuf, sizeof(addrBuf)))
                ni.address = addrBuf;
            ni.isIPv4 = false;
        }
        if (!ni.address.empty()) result.push_back(std::move(ni));
    }

    freeifaddrs(ifap);
    return result;
}

#endif

// ---------------------------------------------------------------------------
// Convenience built on listNetworkInterfaces()
// ---------------------------------------------------------------------------

void printNetworkInterfaces() {
    auto ifs = listNetworkInterfaces();
    logNotice("Network") << "interfaces (" << ifs.size() << "):";
    for (const auto& ni : ifs) {
        logNotice("Network") << "  " << ni.name
            << "  " << ni.address
            << (ni.isIPv4 ? "/v4" : "/v6")
            << (ni.netmask.empty() ? "" : ("  mask=" + ni.netmask))
            << (ni.mac.empty() ? "" : ("  mac=" + ni.mac))
            << (ni.isLoopback ? "  [loopback]" : "")
            << (ni.isUp ? "  [up]" : "  [down]");
    }
}

std::string getLocalIp() {
    auto ifs = listNetworkInterfaces();
    // Prefer an up, non-loopback, private IPv4 (the usual LAN address).
    for (const auto& ni : ifs)
        if (ni.isUp && !ni.isLoopback && ni.isIPv4 && isPrivate(ni.address)) return ni.address;
    // Then any up, non-loopback IPv4.
    for (const auto& ni : ifs)
        if (ni.isUp && !ni.isLoopback && ni.isIPv4) return ni.address;
    // Then any up, non-loopback address (IPv6).
    for (const auto& ni : ifs)
        if (ni.isUp && !ni.isLoopback) return ni.address;
    return "";
}

std::vector<std::string> getLocalIps() {
    std::vector<std::string> out;
    for (const auto& ni : listNetworkInterfaces())
        if (ni.isUp && !ni.isLoopback) out.push_back(ni.address);
    return out;
}

} // namespace trussc
