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
#include "../../../../addons/tcxOsc/src/tcxOscMessage.h"
#include "../../../../addons/tcxOsc/src/tcxOscBundle.h"
TC_CORE_TEST_MAIN() {
    using namespace tcx::osc;
    vector<uint8_t> blob{0, 1, 128, 255};
    OscMessage message("/span");
    check("blob chain return", &message.addBlob(blob) == &message);
    auto bytes = message.toBytes();
    bool ok = false, paddingMissing = true;
    auto parsed = OscMessage::fromBytes(bytes, ok);
    check("vector parse round trip", ok && parsed.toBytes() == bytes);
    parsed = OscMessage::fromBytes(bytes, ok, paddingMissing);
    check("vector parse padding result", ok && !paddingMissing && parsed.toBytes() == bytes);
    bytes.pop_back();
    OscMessage::fromBytes(bytes, ok);
    check("truncated input keeps parser bounds", !ok);
    OscMessage::fromBytes(nullptr, 0, ok);
    check("pointer null input remains available", !ok);
    OscBundle bundle;
    bundle.addMessage(message);
    bytes = bundle.toBytes();
    auto parsedBundle = OscBundle::fromBytes(bytes, ok);
    check("bundle vector round trip", ok && parsedBundle.toBytes() == bytes);
    parsedBundle = OscBundle::fromBytes(bytes, ok, paddingMissing);
    check("bundle padding result", ok && !paddingMissing && parsedBundle.toBytes() == bytes);
    bytes.pop_back();
    OscBundle::fromBytes(bytes, ok);
    check("bundle truncated input keeps bounds", !ok);
    OscBundle::fromBytes(nullptr, 0, ok);
    check("bundle pointer null remains available", !ok);
    return failures ? 1 : 0;
}
