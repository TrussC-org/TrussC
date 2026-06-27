#pragma once
#include "tc/utils/tcAnnotations.h"

// =============================================================================
// tcNetworkInterface.h - Network interface enumeration
//
// Cross-platform listing of the host's network interfaces (NICs, loopback,
// virtual/VPN adapters) with their IP / netmask / MAC.
//   - Windows:      GetAdaptersAddresses()  (iphlpapi)
//   - macOS/Linux:  getifaddrs()
//   - Web (wasm):   not available -> returns empty, logs a warning
//
// Loosely coupled by design: enumeration only. To bind/send on a specific
// interface, hand the `address` string to the existing socket APIs
// (e.g. UdpSocket::bind / UdpSocket::setMulticastInterface).
//
// Note: on Android the `mac` field is typically empty — since Android 6 apps
// cannot read hardware MAC addresses (privacy restriction), so the field falls
// back to its "empty if unavailable" contract. IP/netmask enumeration works.
// =============================================================================

#include <string>
#include <vector>

namespace trussc {

// ---------------------------------------------------------------------------
// NetworkInterface - one address entry of one interface
//
// One interface can yield several entries (e.g. an IPv4 and an IPv6 address);
// each is reported separately. `mac` is the hardware address of the owning
// interface (may be empty for loopback / virtual interfaces).
// ---------------------------------------------------------------------------
struct NetworkInterface {
    std::string name;        // "en0" / "Ethernet" / "wlan0"
    std::string address;     // "192.168.1.42" or IPv6 textual form
    std::string netmask;     // "255.255.255.0" (IPv4); IPv6 prefix as a mask if known
    std::string mac;         // "a4:83:e7:11:22:33" (lowercase, empty if unavailable)
    bool isIPv4 = true;      // false = IPv6
    bool isLoopback = false; // 127.0.0.0/8 or ::1
    bool isUp = true;        // interface link is up

    const std::string& getName() const { return name; }
    const std::string& getAddress() const { return address; }
    const std::string& getNetmask() const { return netmask; }
    const std::string& getMac() const { return mac; }
    bool getIsIPv4() const { return isIPv4; }
    bool getIsLoopback() const { return isLoopback; }
    bool getIsUp() const { return isUp; }
};

// --- Enumeration ---

// All address entries of all interfaces (IPv4 + IPv6 + loopback, up or down).
// Filter with the struct flags / the helpers below for a specific need.
TC_PLATFORMS("macos,windows,linux,android,ios") std::vector<NetworkInterface> listNetworkInterfaces();

// Log the interface list via logNotice() (one line per entry).
TC_PLATFORMS("macos,windows,linux,android,ios") void printNetworkInterfaces();

// --- Convenience ---

// The single "most likely LAN address": skips loopback / down, IPv4 preferred,
// private ranges preferred. Returns "" if nothing suitable was found.
TC_PLATFORMS("macos,windows,linux,android,ios") std::string getLocalIp();

// Every non-loopback address (one per NIC entry). Useful on multi-homed hosts.
TC_PLATFORMS("macos,windows,linux,android,ios") std::vector<std::string> getLocalIps();

// --- Address classification helpers (string in, no socket required) ---

bool isLoopback(const std::string& addr);   // 127.0.0.0/8 or ::1
bool isPrivate(const std::string& addr);     // 10/8, 172.16/12, 192.168/16
bool isLinkLocal(const std::string& addr);   // 169.254/16 (IPv4) or fe80::/10 (IPv6)

// True if a, b share the same IPv4 subnet under netmask. False for non-IPv4 args.
bool sameSubnet(const std::string& a, const std::string& b, const std::string& netmask);

// --- MAC helpers (no vendor database required) ---

// The OUI (first 3 bytes) of a MAC, uppercase "A4:83:E7". "" if unparseable.
// Vendor-name lookup needs the IEEE OUI database and is intentionally left to
// an optional addon, not core.
std::string getOui(const std::string& mac);

// True if the MAC's locally-administered bit is set (bit 1 of the first octet),
// i.e. a randomized / virtual MAC rather than a vendor-assigned one.
bool isLocallyAdministered(const std::string& mac);

} // namespace trussc
