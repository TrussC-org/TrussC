#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
namespace {
using namespace tc;
using namespace std;
int failures = 0;
void check(const char* label, bool ok) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
} // namespace
TC_CORE_TEST_MAIN() {
    UdpSocket sender, receiver;
    vector<unsigned char> empty;
    check("invalid socket span receive retains error", receiver.receive(empty) == -1);
    check("pointer nullptr zero remains available", receiver.receive(nullptr, 0) == -1);
    receiver.setBindAddress("127.0.0.1");
    if (!receiver.bind(0, false)) {
        puts("SKIP: UDP loopback sockets unavailable");
        return 77;
    }
    check("receive timeout", receiver.setReceiveTimeout(2000));
    vector<unsigned char> sent{0, 1, 128, 255}, received(8, 42);
    check("vector send", sender.sendTo("127.0.0.1", receiver.getLocalPort(), sent));
    string host;
    int port = 0;
    check("vector receive count and sender", receiver.receive(received, host, port) == 4 && host == "127.0.0.1" && port > 0);
    check("receive respects count", equal(sent.begin(), sent.end(), received.begin()) && received[4] == 42);
    check("pointer send remains available", sender.sendTo("127.0.0.1", receiver.getLocalPort(), sent.data(), sent.size()));
    check("simple vector receive count", receiver.receive(received) == 4);
    check("string literal send remains available", sender.sendTo("127.0.0.1", receiver.getLocalPort(), "test"));
    check("pointer receive remains available", receiver.receive(received.data(), received.size()) == 4);
    check("connect", sender.connect("127.0.0.1", receiver.getLocalPort()));
    check("connected vector send", sender.send(sent));
    check("connected send receive count", receiver.receive(received) == 4 && equal(sent.begin(), sent.end(), received.begin()));
    return failures ? 1 : 0;
}
