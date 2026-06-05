// =============================================================================
// tcxOsc tests - headless behavioral test (no window).
//
// Built and run by CI on every push/PR across macOS / Windows / Linux via
// examples/build_all.py --addon-tests-only (exit 0 = pass, non-zero = fail).
// Console only, so it runs on headless runners. Uses tc::UdpSocket through
// OscSender/OscReceiver (not raw POSIX sockets) so it compiles on Windows too.
//
// Beyond OSC framing, this is also the cross-platform proof for the core IPv4
// MULTICAST API (UdpSocket::joinMulticastGroup / setMulticastTTL /
// setMulticastLoopback / setReusePort) that OscReceiver/OscSender now use:
//   - unicast loopback still works
//   - a receiver that JOINED a group receives multicast sent to it
//   - a receiver on the same port that did NOT join does NOT receive it
//   - traffic for a different group does NOT leak to a joined receiver
// =============================================================================

#include <tcxOsc.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace tcx;

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static int g_pass = 0, g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-56s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);  // flush each line so CI logs survive a later timeout
    ok ? ++g_pass : ++g_fail;
}

// Send `msg` repeatedly (loopback can drop the first datagram before the receive
// thread is fully on the group) and return the first message the receiver buffers.
static bool sendAndRecv(OscReceiver& rx, OscSender& tx,
                        const std::string& host, int port,
                        const OscMessage& msg, OscMessage& out) {
    rx.hasNewMessage();  // first call enables the polling buffer
    for (int i = 0; i < 40; ++i) {
        tx.sendTo(host, port, msg);
        sleepMs(25);
        if (rx.getNextMessage(out)) return true;
    }
    return false;
}

// Assert the receiver gets NOTHING for ~1s of repeated sends (a scoping check).
static bool sendAndExpectNone(OscReceiver& rx, OscSender& tx,
                              const std::string& host, int port,
                              const OscMessage& msg) {
    rx.hasNewMessage();  // enable buffer so a leak would be caught
    OscMessage scratch;
    for (int i = 0; i < 40; ++i) {
        tx.sendTo(host, port, msg);
        sleepMs(25);
        if (rx.getNextMessage(scratch)) return false;  // leaked!
    }
    return true;
}

int main() {
    const std::string GROUP_A = "239.77.0.1";
    const std::string GROUP_B = "239.77.0.2";
    const int PORT_UNI  = 57110;
    const int PORT_MC   = 57111;  // joined receiver
    const int PORT_NONE = 57112;  // non-member receiver

    OscSender tx;
    tx.setMulticastTTL(1);          // stay on local subnet
    tx.setMulticastLoopback(true);  // deliver to local listeners on this host

    // ----- 1. unicast loopback (baseline) ------------------------------------
    {
        OscReceiver rx;
        bool bound = rx.setup(PORT_UNI);
        check("unicast: receiver bound", bound);

        OscMessage m("/uni");
        m.addInt(42);
        OscMessage got;
        bool ok = sendAndRecv(rx, tx, "127.0.0.1", PORT_UNI, m, got);
        check("unicast: message received", ok);
        check("unicast: address == /uni", ok && got.getAddress() == "/uni");
        check("unicast: arg == 42", ok && got.getArgCount() == 1 && got.getArgAsInt(0) == 42);
        rx.close();
    }

    // ----- 2. multicast delivery to a joined group ---------------------------
    {
        OscReceiver rx;
        check("multicast: receiver bound", rx.setup(PORT_MC));
        check("multicast: joinMulticast(A)", rx.joinMulticast(GROUP_A));
        sleepMs(100);  // let the join settle

        OscMessage m("/mc");
        m.addInt(7);
        OscMessage got;
        bool ok = sendAndRecv(rx, tx, GROUP_A, PORT_MC, m, got);
        check("multicast: joined receiver got it", ok);
        check("multicast: address == /mc", ok && got.getAddress() == "/mc");
        check("multicast: arg == 7", ok && got.getArgCount() == 1 && got.getArgAsInt(0) == 7);

        // 3. scoping by GROUP: traffic for group B must not reach a B-less receiver.
        OscMessage mb("/other");
        mb.addInt(99);
        bool noLeak = sendAndExpectNone(rx, tx, GROUP_B, PORT_MC, mb);
        check("scoping: group-B traffic does not leak to group-A receiver", noLeak);
        rx.close();
    }

    // ----- 4. scoping by MEMBERSHIP: same group, receiver did NOT join --------
    {
        OscReceiver rx;
        check("membership: receiver bound (no join)", rx.setup(PORT_NONE));
        sleepMs(100);

        OscMessage m("/mc");
        m.addInt(7);
        bool noLeak = sendAndExpectNone(rx, tx, GROUP_A, PORT_NONE, m);
        check("scoping: non-member receives nothing (join required)", noLeak);
        rx.close();
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
