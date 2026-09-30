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
//
// It also checks bundle parsing and dispatch:
//   - bundle nesting is limited to OscBundle::MAX_NESTING_DEPTH levels; past
//     that, or when a nested bundle fails, the whole packet is one parse error
//   - dispatch hands listeners the parsed bundles themselves, not copies
// =============================================================================

#include <tcxOsc.h>

#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

// A bundle `levels` deep (the outermost is level 1). Level n holds the message
// "/level/n" followed by the level n+1 bundle, so every level has a message.
static OscBundle makeNestedBundle(int levels) {
    OscBundle inner;
    inner.addMessage(OscMessage("/level/" + std::to_string(levels)));
    for (int n = levels - 1; n >= 1; --n) {
        OscBundle outer;
        outer.addMessage(OscMessage("/level/" + std::to_string(n)));
        outer.addBundle(inner);
        inner = std::move(outer);
    }
    return inner;
}

// Messages in a parsed bundle tree, walked through bundleAt() (no copies).
static int countMessages(const OscBundle& b) {
    int n = 0;
    for (size_t i = 0; i < b.getElementCount(); ++i) {
        if (b.isMessage(i)) ++n;
        else if (const OscBundle* child = b.bundleAt(i)) n += countMessages(*child);
    }
    return n;
}

// A valid message, then a nested bundle that ends before its timetag
// ("#bundle\0" plus 4 bytes, where 16 is the minimum), so it cannot parse.
static std::vector<uint8_t> bundleWithShortChild() {
    OscBundle outer;
    outer.addMessage(OscMessage("/level/1"));
    std::vector<uint8_t> bytes = outer.toBytes();
    const uint8_t child[12] = { '#', 'b', 'u', 'n', 'd', 'l', 'e', '\0', 0, 0, 0, 0 };
    const uint8_t childSize[4] = { 0, 0, 0, sizeof(child) };  // big-endian
    bytes.insert(bytes.end(), childSize, childSize + 4);
    bytes.insert(bytes.end(), child, child + sizeof(child));
    return bytes;
}

int main() {
    const std::string GROUP_A = "239.77.0.1";
    const std::string GROUP_B = "239.77.0.2";
    static const int UNI_PORTS[4] = { 17110, 18110, 19110, 27110 };
    static const int MC_PORTS[4]  = { 17111, 18111, 19111, 27111 };  // joined receiver
    static const int NEST_PORTS[4] = { 17112, 18112, 19112, 27112 };  // bundle nesting
    const int LIMIT = OscBundle::MAX_NESTING_DEPTH;

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

    // ----- 4. bundle nesting limit (parser) ----------------------------------
    // fromBytes accepts LIMIT levels (the outermost is level 1). Past that, or
    // when a nested bundle fails, it rejects the whole bundle.
    {
        bool ok = false;
        std::vector<uint8_t> atLimit = makeNestedBundle(LIMIT).toBytes();
        OscBundle parsed = OscBundle::fromBytes(atLimit.data(), atLimit.size(), ok);
        check("nesting: MAX_NESTING_DEPTH levels parse", ok);
        check("nesting: every level is kept", ok && countMessages(parsed) == LIMIT);

        std::vector<uint8_t> past = makeNestedBundle(LIMIT + 1).toBytes();
        OscBundle::fromBytes(past.data(), past.size(), ok);
        check("nesting: one level past the limit fails", !ok);

        std::vector<uint8_t> shortChild = bundleWithShortChild();
        OscBundle::fromBytes(shortChild.data(), shortChild.size(), ok);
        check("nesting: a failing nested bundle fails the whole", !ok);
    }

    // ----- 5. bundle nesting through OscReceiver -----------------------------
    // Each packet is sent once over unicast loopback, then a "/sync" message.
    // Loopback keeps the order of datagrams from one socket, so once the sync
    // arrives the receive thread has finished the packet before it.
    {
        OscReceiver rx;
        const int port = bindFirstFree(rx, NEST_PORTS);
        check("nesting: receiver bound", port != 0);

        std::mutex mtx;  // guards the counters below (listeners run on the receive thread)
        std::map<std::string, int> perAddress;
        int messages = 0, errors = 0, bundles = 0, syncSeen = 0;
        std::vector<const OscBundle*> chain;  // bundles notified for the current packet
        bool sameObjects = true;

        tc::EventListener msgListener = rx.onMessageReceived.listen([&](OscMessage& m) {
            std::lock_guard<std::mutex> lock(mtx);
            if (m.getAddress() == "/sync") { syncSeen = m.getArgAsInt(0); return; }
            ++messages;
            ++perAddress[m.getAddress()];
        });
        tc::EventListener errListener = rx.onParseError.listen([&](std::string&) {
            std::lock_guard<std::mutex> lock(mtx);
            ++errors;
        });
        tc::EventListener bundleListener = rx.onBundleReceived.listen([&](OscBundle& b) {
            std::lock_guard<std::mutex> lock(mtx);
            ++bundles;
            // A nested bundle must be the parent's own element, not a copy of it.
            // (The parent is still being dispatched, so the pointer is live.)
            if (!chain.empty()) {
                const OscBundle* parent = chain.back();
                if (parent->bundleAt(parent->getElementCount() - 1) != &b) sameObjects = false;
            }
            chain.push_back(&b);
        });

        tc::UdpSocket raw;  // sends the bytes exactly as built
        int token = 0;
        // Send `packet` once and wait for the sync after it (false on timeout).
        auto deliver = [&](const std::vector<uint8_t>& packet) {
            {
                std::lock_guard<std::mutex> lock(mtx);
                perAddress.clear();
                messages = errors = bundles = 0;
                chain.clear();
                sameObjects = true;
            }
            ++token;
            raw.sendTo("127.0.0.1", port, packet.data(), packet.size());
            OscMessage sync("/sync");
            sync.addInt(token);
            std::vector<uint8_t> syncBytes = sync.toBytes();
            for (int i = 0; i < 80; ++i) {
                if (i % 10 == 0) raw.sendTo("127.0.0.1", port, syncBytes.data(), syncBytes.size());
                sleepMs(25);
                std::lock_guard<std::mutex> lock(mtx);
                if (syncSeen == token) return true;
            }
            return false;
        };

        if (port != 0) {
            // 10 levels, one message per level
            bool got = deliver(makeNestedBundle(10).toBytes());
            {
                std::lock_guard<std::mutex> lock(mtx);
                bool eachOnce = perAddress.size() == 10;
                for (int n = 1; n <= 10; ++n) {
                    auto it = perAddress.find("/level/" + std::to_string(n));
                    eachOnce = eachOnce && it != perAddress.end() && it->second == 1;
                }
                check("nesting: 10 levels: each message delivered once", got && eachOnce && messages == 10);
                check("nesting: 10 levels: one onBundleReceived per level", got && bundles == 10);
                check("nesting: 10 levels: no parse error", got && errors == 0);
                check("dispatch: nested bundles passed by reference", got && bundles == 10 && sameObjects);
            }

            // Exactly at the limit
            got = deliver(makeNestedBundle(LIMIT).toBytes());
            {
                std::lock_guard<std::mutex> lock(mtx);
                check("nesting: MAX_NESTING_DEPTH levels delivered",
                      got && messages == LIMIT && bundles == LIMIT && errors == 0);
            }

            // One level past the limit
            got = deliver(makeNestedBundle(LIMIT + 1).toBytes());
            {
                std::lock_guard<std::mutex> lock(mtx);
                check("nesting: past the limit: onParseError once", got && errors == 1);
                check("nesting: past the limit: nothing delivered", got && messages == 0 && bundles == 0);
            }

            // A nested bundle that fails to parse
            got = deliver(bundleWithShortChild());
            {
                std::lock_guard<std::mutex> lock(mtx);
                check("nesting: failing nested bundle: onParseError once", got && errors == 1);
                check("nesting: failing nested bundle: nothing delivered", got && messages == 0 && bundles == 0);
            }
        }
        rx.close();
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
