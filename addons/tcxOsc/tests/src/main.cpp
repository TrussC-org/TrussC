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
//
// And size checks in the parser:
//   - a bundle element or blob whose size runs past the end of the data
//     fails to parse (one parse error, nothing delivered)
//   - the largest 32-bit sizes fail too, including one that brings pos + size
//     to exactly 4 GiB (0 when size_t is 32 bits)
//   - valid packets of every argument shape still parse
//
// And the polling queue:
//   - getNextMessage() alone (no hasNewMessage()) enables the queue
//   - a full queue drops the oldest messages and counts them in
//     getDroppedMessages(); the default size (1024) takes a 150-message bundle
//   - drops are logged from the polling calls, at most once every 2 s,
//     summed since the last report
//   - shrinking a filled queue with setBufferSize() counts and reports the
//     discarded messages as dropped
// =============================================================================

#include <tcxOsc.h>

#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>

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

static void appendBe32(std::vector<uint8_t>& bytes, uint32_t v) {
    bytes.push_back(uint8_t(v >> 24));
    bytes.push_back(uint8_t(v >> 16));
    bytes.push_back(uint8_t(v >> 8));
    bytes.push_back(uint8_t(v));
}

// A size field value, from the offset `at` where the sized data starts and the
// number of bytes `left` from there to the end of the data.
using SizeFor = uint32_t (*)(size_t at, size_t left);

// Sizes that must not pass a "fits in the data left" check. 0xFFFFFFFF is
// SIZE_MAX when size_t is 32 bits, and the last one brings at + size to
// exactly 4 GiB, which is 0 there.
struct BadSize { const char* name; SizeFor sizeFor; };
static const BadSize BAD_SIZES[] = {
    { "1 byte past the end", [](size_t, size_t left) { return uint32_t(left + 1); } },
    { "0x7FFFFFFF",          [](size_t, size_t) { return uint32_t(0x7FFFFFFFu); } },
    { "0x80000000",          [](size_t, size_t) { return uint32_t(0x80000000u); } },
    { "0xFFFFFFFF",          [](size_t, size_t) { return uint32_t(0xFFFFFFFFu); } },
    { "4 GiB minus offset",  [](size_t at, size_t) { return uint32_t(0u - uint32_t(at)); } },
};
static uint32_t sizeExactlyLeft(size_t, size_t left) { return uint32_t(left); }

// A bundle holding the message "/ok", then one more element whose size field
// comes from `sizeFor`, followed by only the 4 bytes of the message "/x".
static std::vector<uint8_t> bundleWithElementSize(SizeFor sizeFor) {
    OscBundle outer;
    outer.addMessage(OscMessage("/ok"));
    std::vector<uint8_t> bytes = outer.toBytes();
    const uint8_t element[4] = { '/', 'x', 0, 0 };
    appendBe32(bytes, sizeFor(bytes.size() + 4, sizeof(element)));
    bytes.insert(bytes.end(), element, element + sizeof(element));
    return bytes;
}

// The message "/b" with one blob whose size field comes from `sizeFor`,
// followed by only 4 bytes of blob data.
static std::vector<uint8_t> messageWithBlobSize(SizeFor sizeFor) {
    std::vector<uint8_t> bytes = { '/', 'b', 0, 0, ',', 'b', 0, 0 };
    const uint8_t blob[4] = { 1, 2, 3, 4 };
    appendBe32(bytes, sizeFor(bytes.size() + 4, sizeof(blob)));
    bytes.insert(bytes.end(), blob, blob + sizeof(blob));
    return bytes;
}

// The message "/p" with type tags `tags`, then the argument bytes `args`
// as given (no padding added).
static std::vector<uint8_t> messageWithArgBytes(const char* tags, std::vector<uint8_t> args) {
    std::vector<uint8_t> bytes = { '/', 'p', 0, 0 };
    bytes.push_back(',');
    for (const char* t = tags; *t; ++t) bytes.push_back(uint8_t(*t));
    bytes.push_back(0);
    while (bytes.size() % 4 != 0) bytes.push_back(0);
    bytes.insert(bytes.end(), args.begin(), args.end());
    return bytes;
}

// One message with every argument type, and strings and blobs of each length
// mod 4, so every padding case is covered.
static OscMessage makeEveryShapeMessage() {
    OscMessage m("/shapes");
    m.addInt(-7).addInt(INT32_MAX).addFloat(1.5f).addBool(true).addBool(false);
    for (int len = 0; len <= 4; ++len) m.addString(std::string(size_t(len), char('a' + len)));
    const uint8_t blob[5] = { 1, 2, 3, 4, 5 };
    for (int len = 0; len <= 5; ++len) m.addBlob(blob, size_t(len));
    m.addInt(42);  // an argument after the last blob's padding
    return m;
}

static bool sameMessage(const OscMessage& a, const OscMessage& b) {
    if (a.getAddress() != b.getAddress() || a.getTypeTags() != b.getTypeTags() ||
        a.getArgCount() != b.getArgCount()) return false;
    for (size_t i = 0; i < a.getArgCount(); ++i) {
        bool same = true;
        switch (a.getArgType(i)) {
            case 'i': same = a.getArgAsInt(i) == b.getArgAsInt(i); break;
            case 'f': same = a.getArgAsFloat(i) == b.getArgAsFloat(i); break;
            case 's': same = a.getArgAsString(i) == b.getArgAsString(i); break;
            case 'b': same = a.getArgAsBlob(i) == b.getArgAsBlob(i); break;
            default:  same = a.getArgAsBool(i) == b.getArgAsBool(i); break;
        }
        if (!same) return false;
    }
    return true;
}

// A bundle of every shape: the every-shape message, messages with addresses
// of each length mod 4, an empty bundle and a nested bundle with a message.
static OscBundle makeEveryShapeBundle() {
    OscBundle inner;
    inner.addMessage(OscMessage("/inner"));
    OscBundle outer;
    outer.addMessage(makeEveryShapeMessage());
    for (const char* addr : { "/a", "/ab", "/abc", "/abcd" }) outer.addMessage(OscMessage(addr));
    outer.addBundle(OscBundle());
    outer.addBundle(inner);
    return outer;
}

int main() {
    const std::string GROUP_A = "239.77.0.1";
    const std::string GROUP_B = "239.77.0.2";
    static const int UNI_PORTS[4] = { 17110, 18110, 19110, 27110 };
    static const int MC_PORTS[4]  = { 17111, 18111, 19111, 27111 };  // joined receiver
    static const int NEST_PORTS[4] = { 17112, 18112, 19112, 27112 };  // bundle nesting
    static const int SIZE_PORTS[4] = { 17113, 18113, 19113, 27113 };  // size checks
    static const int POLL_PORTS[4] = { 17114, 18114, 19114, 27114 };  // getNextMessage() only
    static const int QUEUE_PORTS[4] = { 17115, 18115, 19115, 27115 }; // queue overflow
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

    // ----- 6. size checks (parser) -------------------------------------------
    // A packet parses only if every element and blob is there in full; valid
    // packets of every shape parse as before.
    {
        bool ok = false;

        // Valid packets
        OscMessage shapes = makeEveryShapeMessage();
        std::vector<uint8_t> bytes = shapes.toBytes();
        OscMessage parsedMsg = OscMessage::fromBytes(bytes.data(), bytes.size(), ok);
        check("sizes: every argument shape round-trips", ok && sameMessage(parsedMsg, shapes));

        const uint8_t addressOnly[4] = { '/', 'a', 0, 0 };  // no type tag string
        parsedMsg = OscMessage::fromBytes(addressOnly, sizeof(addressOnly), ok);
        check("sizes: message without type tags parses", ok && parsedMsg.getAddress() == "/a");

        OscBundle shapesBundle = makeEveryShapeBundle();
        bytes = shapesBundle.toBytes();
        OscBundle parsed = OscBundle::fromBytes(bytes.data(), bytes.size(), ok);
        bool sameTree = ok && parsed.getElementCount() == 7 &&
                        sameMessage(parsed.getMessageAt(0), makeEveryShapeMessage()) &&
                        parsed.getMessageAt(4).getAddress() == "/abcd" &&
                        parsed.bundleAt(5) && parsed.bundleAt(5)->getElementCount() == 0 &&
                        parsed.bundleAt(6) && parsed.bundleAt(6)->getMessageAt(0).getAddress() == "/inner";
        check("sizes: bundle of every shape round-trips", sameTree);

        bytes = OscBundle().toBytes();
        parsed = OscBundle::fromBytes(bytes.data(), bytes.size(), ok);
        check("sizes: empty bundle parses", ok && parsed.getElementCount() == 0);

        // Sizes that use exactly the data left
        bytes = bundleWithElementSize(sizeExactlyLeft);
        parsed = OscBundle::fromBytes(bytes.data(), bytes.size(), ok);
        check("sizes: bundle element that ends at the end parses",
              ok && parsed.getElementCount() == 2 && parsed.getMessageAt(1).getAddress() == "/x");

        bytes = messageWithBlobSize(sizeExactlyLeft);
        parsedMsg = OscMessage::fromBytes(bytes.data(), bytes.size(), ok);
        check("sizes: blob that ends at the end parses",
              ok && parsedMsg.getArgAsBlob(0) == std::vector<uint8_t>({ 1, 2, 3, 4 }));

        // Sizes past the end
        for (const BadSize& bad : BAD_SIZES) {
            bytes = bundleWithElementSize(bad.sizeFor);
            parsed = OscBundle::fromBytes(bytes.data(), bytes.size(), ok);
            check(("sizes: bundle element size " + std::string(bad.name) + " fails").c_str(),
                  !ok && parsed.getElementCount() == 0);

            bytes = messageWithBlobSize(bad.sizeFor);
            OscMessage::fromBytes(bytes.data(), bytes.size(), ok);
            check(("sizes: blob size " + std::string(bad.name) + " fails").c_str(), !ok);
        }

        // A size field cut short after the last element
        bool allFail = true;
        for (int extra = 1; extra <= 3; ++extra) {
            bytes = shapesBundle.toBytes();
            bytes.insert(bytes.end(), size_t(extra), uint8_t(0));
            OscBundle::fromBytes(bytes.data(), bytes.size(), ok);
            allFail = allFail && !ok;
        }
        check("sizes: bundle with 1-3 bytes after the last element fails", allFail);

        // An argument after padding that runs past the end: the string "abcde"
        // and the 5-byte blob each need 3 more bytes of padding, so the int
        // after them has no data.
        bytes = messageWithArgBytes("si", { 'a', 'b', 'c', 'd', 'e', 0 });
        OscMessage::fromBytes(bytes.data(), bytes.size(), ok);
        check("sizes: int after a string cut in its padding fails", !ok);

        bytes = messageWithArgBytes("bi", { 0, 0, 0, 5, 1, 2, 3, 4, 5 });
        OscMessage::fromBytes(bytes.data(), bytes.size(), ok);
        check("sizes: int after a blob cut in its padding fails", !ok);
    }

    // ----- 7. size checks through OscReceiver --------------------------------
    // Same sync scheme as section 5: each packet, then a "/sync" message.
    {
        OscReceiver rx;
        const int port = bindFirstFree(rx, SIZE_PORTS);
        check("sizes: receiver bound", port != 0);

        std::mutex mtx;  // guards the counters below (listeners run on the receive thread)
        int messages = 0, errors = 0, bundles = 0, syncSeen = 0;
        tc::EventListener msgListener = rx.onMessageReceived.listen([&](OscMessage& m) {
            std::lock_guard<std::mutex> lock(mtx);
            if (m.getAddress() == "/sync") { syncSeen = m.getArgAsInt(0); return; }
            ++messages;
        });
        tc::EventListener errListener = rx.onParseError.listen([&](std::string&) {
            std::lock_guard<std::mutex> lock(mtx);
            ++errors;
        });
        tc::EventListener bundleListener = rx.onBundleReceived.listen([&](OscBundle&) {
            std::lock_guard<std::mutex> lock(mtx);
            ++bundles;
        });

        tc::UdpSocket raw;
        int token = 0;
        auto deliver = [&](const std::vector<uint8_t>& packet) {
            {
                std::lock_guard<std::mutex> lock(mtx);
                messages = errors = bundles = 0;
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
        // Exactly one parse error and nothing delivered
        auto rejected = [&](const std::vector<uint8_t>& packet) {
            bool got = deliver(packet);
            std::lock_guard<std::mutex> lock(mtx);
            return got && errors == 1 && messages == 0 && bundles == 0;
        };

        if (port != 0) {
            // 6 messages (the every-shape one, 4 addresses, "/inner"), 3 bundles
            bool got = deliver(makeEveryShapeBundle().toBytes());
            {
                std::lock_guard<std::mutex> lock(mtx);
                check("sizes: bundle of every shape delivered",
                      got && messages == 6 && bundles == 3 && errors == 0);
            }

            got = deliver(bundleWithElementSize(sizeExactlyLeft));
            {
                std::lock_guard<std::mutex> lock(mtx);
                check("sizes: element that ends at the end delivered",
                      got && messages == 2 && bundles == 1 && errors == 0);
            }

            for (const BadSize& bad : BAD_SIZES) {
                check(("sizes: rx: bundle element size " + std::string(bad.name) + " rejected").c_str(),
                      rejected(bundleWithElementSize(bad.sizeFor)));
            }
            check("sizes: rx: blob past the end rejected",
                  rejected(messageWithBlobSize(BAD_SIZES[0].sizeFor)));
        }
        rx.close();
    }

    // ----- 8. polling with getNextMessage() only -----------------------------
    // No hasNewMessage() call anywhere: getNextMessage() has to enable the
    // queue by itself (sendAndRecv() would enable it through hasNewMessage()).
    {
        OscReceiver rx;
        const int port = bindFirstFree(rx, POLL_PORTS);
        check("poll: receiver bound", port != 0);

        OscMessage m("/poll");
        m.addInt(5);
        OscMessage got;
        bool received = false;
        for (int i = 0; port != 0 && i < 40 && !received; ++i) {
            tx.sendTo("127.0.0.1", port, m);
            sleepMs(25);
            received = rx.getNextMessage(got);
        }
        check("poll: getNextMessage() alone receives",
              received && got.getAddress() == "/poll" && got.getArgCount() == 1 &&
              got.getArgAsInt(0) == 5);
        rx.close();
    }

    // ----- 9. queue overflow: drop the oldest, count, rate-limited log --------
    {
        // Log lines from tcxOsc, and whether each came from this (the polling) thread
        std::mutex logMtx;
        std::vector<std::string> oscLogs;
        bool logOffThread = false;
        const std::thread::id mainThread = std::this_thread::get_id();
        tc::EventListener logListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& e) {
            if (e.message.find("[tcxOsc]") == std::string::npos) return;
            std::lock_guard<std::mutex> lock(logMtx);
            oscLogs.push_back(e.message);
            if (std::this_thread::get_id() != mainThread) logOffThread = true;
        });
        auto logCount = [&] {
            std::lock_guard<std::mutex> lock(logMtx);
            return oscLogs.size();
        };
        auto lastLog = [&] {
            std::lock_guard<std::mutex> lock(logMtx);
            return oscLogs.empty() ? std::string() : oscLogs.back();
        };
        auto has = [](const std::string& s, const std::string& part) {
            return s.find(part) != std::string::npos;
        };

        const int COUNT = 150;
        OscBundle bundle;
        for (int i = 0; i < COUNT; ++i) {
            OscMessage m("/q");
            m.addInt(i);
            bundle.addMessage(m);
        }

        OscReceiver rx;
        const int port = bindFirstFree(rx, QUEUE_PORTS);
        check("queue: receiver bound", port != 0);
        check("queue: default size is 1024", rx.getBufferSize() == 1024);

        // Counts "/q" messages as the receive thread dispatches them. The
        // queue push for a message happens before its listener runs, so once
        // the listener has seen all COUNT, the queue and counters are final.
        std::mutex mtx;
        int seen = 0;
        tc::EventListener msgListener = rx.onMessageReceived.listen([&](OscMessage& m) {
            if (m.getAddress() != "/q") return;
            std::lock_guard<std::mutex> lock(mtx);
            ++seen;
        });
        // Send the bundle and wait until all of it is dispatched, without
        // draining. A datagram arrives whole or not at all, so resend only
        // when nothing arrived (loopback can drop one).
        auto deliverBundle = [&]() {
            {
                std::lock_guard<std::mutex> lock(mtx);
                seen = 0;
            }
            for (int attempt = 0; attempt < 5; ++attempt) {
                tx.sendTo("127.0.0.1", port, bundle);
                for (int i = 0; i < 40; ++i) {
                    sleepMs(25);
                    std::lock_guard<std::mutex> lock(mtx);
                    if (seen == COUNT) return true;
                }
                std::lock_guard<std::mutex> lock(mtx);
                if (seen > 0) return false;  // part of a bundle: should not happen
            }
            return false;
        };
        // Drain the queue with getNextMessage() and return the int args in
        // order. `received` counts every message handed out in this section.
        uint64_t received = 0;
        auto drain = [&]() {
            std::vector<int> ids;
            OscMessage m;
            while (rx.getNextMessage(m)) ids.push_back(m.getArgAsInt(0));
            received += ids.size();
            return ids;
        };

        if (port != 0) {
            // Default size: the whole bundle fits
            OscMessage scratch;
            rx.getNextMessage(scratch);  // enables the queue
            bool got = deliverBundle();
            std::vector<int> ids = drain();
            check("queue: default size keeps a 150-message bundle",
                  got && ids.size() == size_t(COUNT) && ids.front() == 0 && ids.back() == COUNT - 1);
            check("queue: default size drops nothing", got && rx.getDroppedMessages() == 0);
            check("queue: default size logs nothing", logCount() == 0);

            // Explicit size 100: the 50 oldest of the bundle are dropped
            rx.setBufferSize(100);
            check("queue: setBufferSize(100)", rx.getBufferSize() == 100);
            got = deliverBundle();
            ids = drain();
            const auto firstReport = std::chrono::steady_clock::now();
            check("queue: 100 of 150 kept", got && ids.size() == 100);
            check("queue: dropped == 50", got && rx.getDroppedMessages() == 50);
            check("queue: received + dropped == 150",
                  got && ids.size() + rx.getDroppedMessages() == size_t(COUNT));
            check("queue: the oldest are the ones dropped",
                  got && ids.size() == 100 && ids.front() == 50 && ids.back() == COUNT - 1);
            const std::string line = lastLog();
            check("queue: drop logged once by the polling call", logCount() == 1);
            check("queue: log gives the count, size and setBufferSize()",
                  has(line, " 50 messages dropped") && has(line, "queue limit 100") &&
                  has(line, "setBufferSize()"));

            // A second overflow inside 2 s is counted but not logged yet
            got = deliverBundle();
            ids = drain();
            const bool within = std::chrono::steady_clock::now() - firstReport <
                                std::chrono::milliseconds(1500);
            check("queue: second overflow counted (total 100)",
                  got && ids.size() == 100 && rx.getDroppedMessages() == 100);
            if (within) {
                check("queue: no second log line within 2 s", logCount() == 1);
            } else {
                std::printf("%-56s %s\n", "queue: rate-limit check SKIPPED (delivery too slow)", "SKIP");
                std::fflush(stdout);
            }

            // After 2 s the next polling call logs the drops summed since
            std::this_thread::sleep_until(firstReport + std::chrono::milliseconds(2100));
            rx.hasNewMessage();
            const auto secondReport = std::chrono::steady_clock::now();
            check("queue: next report after 2 s", logCount() == 2);
            check("queue: next report sums the drops since the last",
                  has(lastLog(), " 50 messages dropped"));
            rx.hasNewMessage();
            check("queue: nothing new, nothing logged", logCount() == 2);

            // Shrinking a filled queue: the discarded messages count as dropped
            rx.setBufferSize(1024);
            got = deliverBundle();  // fits whole, not drained
            check("queue: refill without drops", got && rx.getDroppedMessages() == 100);
            rx.setBufferSize(40);
            check("queue: shrink to 40 counts the 110 discarded",
                  got && rx.getDroppedMessages() == 210);
            std::this_thread::sleep_until(secondReport + std::chrono::milliseconds(2100));
            ids = drain();
            check("queue: shrink keeps the newest 40",
                  got && ids.size() == 40 && ids.front() == 110 && ids.back() == COUNT - 1);
            check("queue: the next poll reports the discarded ones",
                  logCount() == 3 && has(lastLog(), " 110 messages dropped") &&
                  has(lastLog(), "queue limit 40"));
            // 4 bundles delivered in this section
            check("queue: received + dropped == sent over the section",
                  got && received + rx.getDroppedMessages() == uint64_t(4 * COUNT));
            check("queue: drops logged only on the polling thread", !logOffThread);
        }
        rx.close();
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
