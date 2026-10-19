// =============================================================================
// core/tests/networkInterface — behavioral test for the network interface
// enumeration API (tc::listNetworkInterfaces and the address/MAC helpers).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards: the pure classification helpers (isLoopback / isPrivate / isLinkLocal
// / sameSubnet / getOui / isLocallyAdministered) have fixed, platform-independent
// answers; and enumeration returns at least a loopback entry on any real host.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-58s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

} // namespace

TC_CORE_TEST_MAIN() {
    // --- 1. isLoopback ---
    check("isLoopback 127.0.0.1",        isLoopback("127.0.0.1"));
    check("isLoopback 127.5.6.7",        isLoopback("127.5.6.7"));
    check("isLoopback ::1",              isLoopback("::1"));
    check("isLoopback 192.168.1.1 false", !isLoopback("192.168.1.1"));

    // --- 2. isPrivate ---
    check("isPrivate 10.1.2.3",          isPrivate("10.1.2.3"));
    check("isPrivate 172.16.0.1",        isPrivate("172.16.0.1"));
    check("isPrivate 172.32.0.1 false",  !isPrivate("172.32.0.1")); // outside 172.16/12
    check("isPrivate 192.168.1.1",       isPrivate("192.168.1.1"));
    check("isPrivate 8.8.8.8 false",     !isPrivate("8.8.8.8"));

    // --- 3. isLinkLocal ---
    check("isLinkLocal 169.254.1.1",     isLinkLocal("169.254.1.1"));
    check("isLinkLocal fe80::1",         isLinkLocal("fe80::1"));
    check("isLinkLocal 10.0.0.1 false",  !isLinkLocal("10.0.0.1"));

    // --- 4. sameSubnet ---
    check("sameSubnet /24 same",   sameSubnet("192.168.1.5", "192.168.1.9", "255.255.255.0"));
    check("sameSubnet /24 diff",  !sameSubnet("192.168.1.5", "192.168.2.9", "255.255.255.0"));
    check("sameSubnet bad arg",   !sameSubnet("192.168.1.5", "::1", "255.255.255.0"));

    // --- 5. MAC helpers ---
    check("getOui colon form",     getOui("a4:83:e7:11:22:33") == "A4:83:E7");
    check("getOui dash form",      getOui("a4-83-e7-11-22-33") == "A4:83:E7");
    check("getOui too short",      getOui("a4:83") == "");
    check("isLocallyAdministered 02:..", isLocallyAdministered("02:00:00:00:00:01"));
    check("isLocallyAdministered a4:.. false", !isLocallyAdministered("a4:83:e7:11:22:33"));

    // --- 6. Enumeration smoke test (any real host has a loopback) ---
    auto ifs = listNetworkInterfaces();
    printNetworkInterfaces();
    bool hasLoopback = false;
    for (const auto& ni : ifs)
        if (ni.isLoopback || isLoopback(ni.address)) { hasLoopback = true; break; }
    check("listNetworkInterfaces: non-empty", !ifs.empty());
    check("listNetworkInterfaces: has loopback", hasLoopback);

    std::printf("\nlocal IP : %s\n", getLocalIp().c_str());
    auto ips = getLocalIps();
    std::printf("local IPs: %zu\n", ips.size());

    std::printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    std::fflush(stdout);
    return g_fail ? 1 : 0;
}
