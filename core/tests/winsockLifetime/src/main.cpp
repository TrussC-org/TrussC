// =============================================================================
// core/tests/winsockLifetime — regression test for Winsock's lifetime (#254).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariant: creating and destroying TcpClient / TcpServer any
// number of times leaves networking working. Each class kept a live-instance
// count, called WSAStartup() once per process but WSACleanup() every time its
// count fell to zero, so every 0 -> 1 -> 0 cycle after the first dropped
// Winsock's process-wide reference count by one. Once the references other
// code held ran out, Winsock was torn down: socket() failed with
// WSANOTINITIALISED and an already-running UdpSocket stopped receiving.
// Winsock is now started once per process and never cleaned up.
//
// The failure is Windows-only. Elsewhere the same steps run and must pass,
// but they cannot catch the regression.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define TC_CLOSE closesocket
    using rawsocket_t = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define TC_CLOSE ::close
    using rawsocket_t = int;
#endif

using namespace std;
using namespace tc;

static const rawsocket_t kNoSocket = static_cast<rawsocket_t>(-1);

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_fail;
}

static int lastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

// A free UDP port on loopback, or 0
static int freeUdpPort() {
    rawsocket_t s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kNoSocket) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    socklen_t len = sizeof(addr);
    int port = 0;
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) == 0 &&
        ::getsockname(s, (sockaddr*)&addr, &len) == 0) {
        port = ntohs(addr.sin_port);
    }
    TC_CLOSE(s);
    return port;
}

int main() {
#ifndef _WIN32
    printf("(Winsock is Windows-only: these steps pass here but cannot catch the regression)\n");
#endif

    // A receiver that is already running when short-lived clients come and
    // go, like an OSC input. Its constructor also starts Winsock, which the
    // raw socket calls below rely on.
    UdpSocket rx;
    const int port = freeUdpPort();
    check("found a free UDP port", port != 0);
    if (port == 0) return 1;

    mutex rxMutex;
    string rxPayload;
    atomic<bool> rxGot{false};
    EventListener rxSub = rx.onReceive.listen([&](UdpReceiveEventArgs& e) {
        lock_guard<mutex> lock(rxMutex);
        rxPayload.assign(e.data.begin(), e.data.end());
        rxGot = true;
    });
    check("UdpSocket binds and starts receiving", rx.bind(port, true));

    // --- the churn ------------------------------------------------------------
    for (int i = 0; i < 200; ++i) { TcpClient c; }
    for (int i = 0; i < 200; ++i) { TcpServer s; }

    // --- networking still works -----------------------------------------------
    rawsocket_t raw = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const int rawErr = raw == kNoSocket ? lastSocketError() : 0;
    if (raw == kNoSocket) printf("  (socket() failed, error %d)\n", rawErr);
    check("a raw socket() still succeeds", raw != kNoSocket);
    if (raw != kNoSocket) TC_CLOSE(raw);

    UdpSocket tx;
    const string msg = "still alive";
    bool sent = false;
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(3);
    // UDP may drop a datagram, so resend until one arrives or time runs out
    while (!rxGot.load() && chrono::steady_clock::now() < deadline) {
        sent = tx.sendTo("127.0.0.1", port, msg) || sent;
        this_thread::sleep_for(chrono::milliseconds(50));
    }
    check("a new UdpSocket can send", sent);
    check("the running UdpSocket receives a loopback packet", rxGot.load());
    if (rxGot.load()) {
        lock_guard<mutex> lock(rxMutex);
        check("the packet arrives intact", rxPayload == msg);
    }

    tx.close();
    rx.close();

    printf("\n%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
