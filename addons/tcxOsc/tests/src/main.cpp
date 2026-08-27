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
//   - traffic for a group NOBODY joined is not received (multicast is join-gated)
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

// Outcome of a send+poll round. Unroutable = every send failed (the host can't
// route this group at all — e.g. macOS CI runners have no multicast route on the
// default NIC), which we treat as "skip", not "fail".
enum class Recv { Received, NotReceived, Unroutable };

// Send `msg` repeatedly (loopback can drop the first datagram before the receive
// thread is fully on the group) and return the first message the receiver buffers.
static Recv sendAndRecv(OscReceiver& rx, OscSender& tx,
                        const std::string& host, int port,
                        const OscMessage& msg, OscMessage& out) {
    rx.hasNewMessage();  // first call enables the polling buffer
    bool anySent = false;
    for (int i = 0; i < 40; ++i) {
        if (tx.sendTo(host, port, msg)) anySent = true;
        sleepMs(25);
        if (rx.getNextMessage(out)) return Recv::Received;
    }
    return anySent ? Recv::NotReceived : Recv::Unroutable;
}

// Check the receiver gets NOTHING for ~1s of repeated sends (a scoping check).
static Recv sendAndExpectNone(OscReceiver& rx, OscSender& tx,
                              const std::string& host, int port,
                              const OscMessage& msg) {
    rx.hasNewMessage();  // enable buffer so a leak would be caught
    OscMessage scratch;
    bool anySent = false;
    for (int i = 0; i < 40; ++i) {
        if (tx.sendTo(host, port, msg)) anySent = true;
        sleepMs(25);
        if (rx.getNextMessage(scratch)) return Recv::Received;  // leaked!
    }
    return anySent ? Recv::NotReceived : Recv::Unroutable;
}

// Bind a receiver to the first candidate port that actually takes it, and
// return that port (0 if none did).
//
// The candidates all sit BELOW 49152 on purpose. At or above that Windows hands
// out dynamic ports, and Hyper-V / WSL / Docker reserve blocks inside that
// range; a bind landing in a reserved block fails with WSAEACCES (10013). A
// hardcoded 57110 did exactly that on the windows-latest CI runner while
// passing everywhere else. Trying several ports also survives an ordinary
// collision with whatever else happens to be listening, so the test never
// depends on one number being free.
//
// They avoid 9000/9001 as well: the tcxOsc examples bind those, and 9000 is
// about the most common OSC port in the wild, so it is the LEAST safe choice
// on a machine that does OSC work.
static int bindFirstFree(OscReceiver& rx, const int (&candidates)[4]) {
    for (int port : candidates) {
        if (rx.setup(port)) return port;
        // setup() creates the socket and registers its listeners even when the
        // bind fails; close() undoes both so the next attempt starts clean.
        rx.close();
    }
    return 0;
}

int main() {
    const std::string GROUP_A = "239.77.0.1";
    const std::string GROUP_B = "239.77.0.2";
    static const int UNI_PORTS[4] = { 17110, 18110, 19110, 27110 };
    static const int MC_PORTS[4]  = { 17111, 18111, 19111, 27111 };  // joined receiver

    // Outgoing multicast interface. macOS CI runners have no multicast route on
    // the default NIC (send -> EHOSTUNREACH), but lo0 is multicast-capable, so
    // route the test over loopback there. Linux/Windows route fine by default
    // (and Linux's lo is not multicast-capable), so leave them on the default.
#if defined(__APPLE__)
    const std::string MIF = "127.0.0.1";
#else
    const std::string MIF = "";  // default route
#endif

    OscSender tx;
    tx.setMulticastTTL(1);            // stay on local subnet
    tx.setMulticastLoopback(true);    // deliver to local listeners on this host
    tx.setMulticastInterface(MIF);

    // ----- 1. unicast loopback (baseline) ------------------------------------
    {
        OscReceiver rx;
        const int portUni = bindFirstFree(rx, UNI_PORTS);
        check("unicast: receiver bound", portUni != 0);
        if (portUni) std::printf("  (unicast port %d)\n", portUni);

        OscMessage m("/uni");
        m.addInt(42);
        OscMessage got;
        bool ok = sendAndRecv(rx, tx, "127.0.0.1", portUni, m, got) == Recv::Received;
        check("unicast: message received", ok);
        check("unicast: address == /uni", ok && got.getAddress() == "/uni");
        check("unicast: arg == 42", ok && got.getArgCount() == 1 && got.getArgAsInt(0) == 42);
        rx.close();
    }

    // ----- 2. multicast delivery to a joined group ---------------------------
    // If the host can't route multicast at all (Recv::Unroutable), skip the
    // multicast assertions rather than fail — but still run them everywhere it
    // IS routable (Linux/Windows CI, local macOS), which is the real coverage.
    {
        OscReceiver rx;
        const int portMc = bindFirstFree(rx, MC_PORTS);
        check("multicast: receiver bound", portMc != 0);
        if (portMc) std::printf("  (multicast port %d)\n", portMc);
        check("multicast: joinMulticast(A)", rx.joinMulticast(GROUP_A, MIF));
        sleepMs(100);  // let the join settle

        OscMessage m("/mc");
        m.addInt(7);
        OscMessage got;
        Recv r = sendAndRecv(rx, tx, GROUP_A, portMc, m, got);
        if (r == Recv::Unroutable) {
            std::printf("%-56s %s\n", "multicast: SKIPPED (no multicast route on this host)", "SKIP");
            std::fflush(stdout);
        } else {
            check("multicast: joined receiver got it", r == Recv::Received);
            check("multicast: address == /mc", r == Recv::Received && got.getAddress() == "/mc");
            check("multicast: arg == 7", r == Recv::Received && got.getArgCount() == 1 && got.getArgAsInt(0) == 7);

            // 3. scoping: traffic for a group NOBODY joined must not arrive — this
            // is what proves multicast is join-gated. (We deliberately do NOT test
            // "a non-member socket on another port" here: IPv4 membership is an
            // interface-level IGMP concept, so once ANY socket on the host joins a
            // group, the kernel may deliver that group's datagrams to other sockets
            // bound to the matching port too — true on Linux. So the robust
            // invariant is per-GROUP, tested with a group that has no members.)
            OscMessage mb("/other");
            mb.addInt(99);
            check("scoping: unjoined group's traffic does not reach the receiver",
                  sendAndExpectNone(rx, tx, GROUP_B, portMc, mb) == Recv::NotReceived);
        }
        rx.close();
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
