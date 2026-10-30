#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
void payloadReleaseOrder(bool throws) {
    int releases = 0;
    auto payload = std::make_shared<Pixels>();
    payload->allocate(160,96,4);
    std::weak_ptr<Pixels> lifetime = payload;
    bool destroyedBeforeRelease = false;
    auto lease = std::shared_ptr<void>(new int(1), [&](void* p) {
        destroyedBeforeRelease = lifetime.expired();
        ++releases;
        delete static_cast<int*>(p);
    });
    auto thunk = mcp::internal::carryCaptureAdmission([payload, throws] {
        if (throws) throw std::runtime_error("encode failed");
        return std::string("encoded");
    }, std::move(lease));
    payload.reset();
    check("payload and credit held until worker execution", !lifetime.expired() && releases == 0);
    bool caught = false;
    try { check("worker result", thunk() == "encoded"); }
    catch (const std::runtime_error&) { caught = true; }
    check("worker exception contract", caught == throws);
    // Keep the delivered thunk alive: completion must empty its source capture.
    check(throws ? "exception destroys payload before credit release" : "completion destroys payload before credit release",
        destroyedBeforeRelease && lifetime.expired() && releases == 1);
    thunk = {};
    check("delivered thunk destruction preserves one release", releases == 1);
}
void productionPayloadReleaseOrder(bool throws) {
    using namespace mcp::internal;
    std::weak_ptr<Pixels> lifetime;
    mcp::ToolBuilder("capture_payload_lifetime", "deferred payload lifetime").bind([&](const Json&) -> Json {
        mcp::deferToolResultTwoStage([&]() -> std::function<Json()> {
            auto payload = std::make_shared<Pixels>();
            payload->allocate(16, 16, 4);
            lifetime = payload;
            return [payload, throws]() -> Json {
                if (throws) throw std::runtime_error("encode failed");
                return {{"status", "ok"}};
            };
        });
        return nullptr;
    });
    const Json request = {{"jsonrpc", "2.0"}, {"id", 821}, {"method", "tools/call"},
        {"params", {{"name", "capture_payload_lifetime"}, {"arguments", Json::object()}}}};
    check("production worker defers", Server::instance().processMessage(request.dump()).empty());
    auto envelope = std::move(deferralState().envelope);
    deferralState().hasEnvelope = false;
    auto thunk = envelope();
    check("production worker retains payload until execution", !lifetime.expired());
    check("production worker returns JSON", !thunk().empty());
    check("production worker releases completed payload", lifetime.expired());
}
} // namespace
TC_CORE_TEST_MAIN() {
    using namespace mcp::internal;
    std::vector<std::shared_ptr<void>> slots;
    for (int i = 0; i < 8; ++i) slots.push_back(acquireScreenshotCredit());
    bool rejected = false;
    try { acquireScreenshotCredit(); } catch (const AdmissionError&) { rejected = true; }
    if (!rejected) return 1;
    auto thunk = carryCaptureAdmission([] { return std::string("saved"); }, std::move(slots.back()));
    slots.pop_back();
    if (thunk() != "saved") return 1;
    slots.push_back(acquireScreenshotCredit());
    slots.clear();
    payloadReleaseOrder(false);
    payloadReleaseOrder(true);
    productionPayloadReleaseOrder(false);
    productionPayloadReleaseOrder(true);
    size_t row = 0, bytes = 0;
    check("source extent layout", internal::captureLayout(160, 96, 4, row, bytes) && row == 640 && bytes == 61440);
    check("nonpositive source rejected", !internal::captureLayout(0, 96, 4, row, bytes) &&
          !internal::captureLayout(160, -1, 4, row, bytes));
    check("row representation rejected before allocation", !internal::captureLayout(INT_MAX, 2, 4, row, bytes));
    check("invalid format rejected before allocation", !internal::captureLayout(4, 4, 0, row, bytes));
    CaptureCompletionGate inProgress;
    bool completed = false;
    inProgress.complete([&] { inProgress.abandon(); completed = true; });
    check("abandonment permits an in-progress consumer", completed);
    CaptureCompletionGate gate;
    gate.abandon();
    bool consumed = false;
    gate.complete([&] { consumed = true; });
    check("abandoned capture skips consumer", !consumed);
    std::puts("PASS: screenshot slots survive deferral and retire on completion");
    return failures ? 1 : 0;
}
