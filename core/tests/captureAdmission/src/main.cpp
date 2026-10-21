#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("captureAdmission %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
#ifndef __EMSCRIPTEN__
void exercise(bool expired, bool throws, bool cancelled) {
    using namespace mcp::internal;
    int releases = 0, issues = 0, encodes = 0;
    auto budget = std::shared_ptr<WorkBudget>(new WorkBudget, [&](WorkBudget* value) { ++releases; delete value; });
    budget->reserve(1024);
    DeferredResponse response;
    response.budget = budget;
    response.id = 821;
    response.response = std::make_shared<std::promise<ReplyThunk>>();
    auto future = response.response->get_future();
    response.target = expired ? &issues : nullptr;
    response.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    response.makeEnvelope = [&] {
        ++issues;
        if (throws) throw std::runtime_error("capture issue failed");
        return carryCaptureAdmission([&] { ++encodes; return std::string("encoded"); }, currentWorkBudget());
    };
    budget.reset();
    if (cancelled) {
        completeTerminalResponse(response.response, response.budget, [] { return std::string("cancelled"); });
        response = {};
    } else {
        { std::lock_guard<std::mutex> lock(deferredResponsesMutex()); deferredResponses().push_back(std::move(response)); }
        drainDeferredResponses();
    }
    const bool workerOwnsBudget = !expired && !cancelled && !throws;
    check("only pending worker retains request budget", releases == (workerOwnsBudget ? 0 : 1));
    auto thunk = future.get();
    const auto reply = thunk();
    check("one terminal budget release", releases == 1);
    check("no downstream issue for expiry/cancel", issues == ((expired || cancelled) ? 0 : 1));
    check("one encode only on successful issue", encodes == (workerOwnsBudget ? 1 : 0));
}
// Exercise the actual Server two-stage worker, including its WorkScope and
// error formatter, rather than just the lifetime helper in isolation.
void productionWorkerReleaseOrder(int throws) {
    using namespace mcp::internal;
    std::weak_ptr<Pixels> lifetime;
    int releases = 0;
    bool destroyedBeforeRelease = false;
    auto budget = std::shared_ptr<WorkBudget>(new WorkBudget, [&](WorkBudget* value) {
        destroyedBeforeRelease = lifetime.expired();
        ++releases;
        delete value;
    });
    ReplyThunk thunk;
    {
        WorkScope scope(budget);
        reserveWork(160 * 96 * 4);
        mcp::ToolBuilder("capture_release_order", "payload lifetime regression").bind([&](const Json&) -> Json {
            mcp::deferToolResultTwoStage([&]() -> std::function<Json()> {
                auto payload = std::make_shared<Pixels>();
                payload->allocate(160,96,4);
                lifetime = payload;
                return [payload, throws]() -> Json {
                    if (throws == 1) throw std::runtime_error("encode failed");
                    if (throws == 2) throw 821;
                    return Json{{"status", "ok"}};
                };
            });
            return nullptr;
        });
        const Json request = {{"jsonrpc", "2.0"}, {"id", 821}, {"method", "tools/call"},
            {"params", {{"name", "capture_release_order"}, {"arguments", Json::object()}}}};
        check("production worker defers", Server::instance().processMessage(request.dump()).empty());
        auto envelope = std::move(deferralState().envelope);
        deferralState().hasEnvelope = false;
        thunk = envelope();
    }
    budget.reset();
    check("production worker holds payload and budget", !lifetime.expired() && releases == 0);
    bool caught = false;
    try { check("production worker returns JSON", !thunk().empty()); }
    catch (int) { caught = true; }
    check("production unknown exception contract", caught == (throws == 2));
    check("production payload destroyed before budget credit", destroyedBeforeRelease && lifetime.expired() && releases == 1);
    thunk = {};
    check("production delivered thunk releases once", releases == 1);
}
void delayedCompletionAccounting(bool save) {
    using namespace mcp::internal;
    const size_t before = workingMemory();
    struct Job {
        std::shared_ptr<WindowCaptureAdmission::Ownership> admission;
        std::function<void(int)> completion;
        std::function<void()> finished;
    } job;
    CaptureCompletionGate result;
    std::weak_ptr<void> slot;
    int allocations = 0, terminals = 0;
    {
        WorkScope request;
        auto credit = save ? acquireScreenshotCredit() : std::shared_ptr<void>{};
        slot = credit;
        reserveWindowCapture(160, 96);
        WindowCaptureAdmission capture(credit);
        reconcileWindowCapture(160, 96);
        job.admission = WindowCaptureAdmission::retain();
        job.completion = [&](int) { result.complete([&] { ++allocations; }); };
        job.finished = [&] {
            check("terminal follows native accounting cleanup", workingMemory() == before && slot.expired());
            ++terminals;
        };
        // Inject timeout before delivering the native completion through the
        // same terminal seam used by the Metal adapter.
        result.abandon();
    }
    check("late completion retains working reservation", workingMemory() > before);
    check("late save completion retains existing slot", !save || !slot.expired());
    finishCaptureJob(job, 0, [&] {
        check("native cleanup still owns working reservation", workingMemory() > before);
        check("native cleanup still owns existing save slot", !save || !slot.expired());
    });
    check("abandoned completion skips output allocation", allocations == 0);
    check("late completion settles once", terminals == 1 && workingMemory() == before && slot.expired());
}
void sourceExtentAccounting() {
    using namespace mcp::internal;
    const size_t before = workingMemory();
    const size_t small = 16 * 16 * 8, large = 160 * 96 * 8;
    {
        WorkScope request;
        reserveWindowCapture(16, 16);
        reconcileWindowCapture(160, 96);
        auto job = WindowCaptureAdmission::retain();
        check("selected source increases smaller reservation", workingMemory() == before + large);
        reconcileWindowCapture(160, 96);
        check("selected source extent is reserved once", workingMemory() == before + large);
    }
    check("selected source reservation retires", workingMemory() == before);
    {
        WorkScope request;
        const size_t limit = mcp::getAdmissionLimits().workingBytes;
        reserveWork(limit - 2 * small);
        reserveWindowCapture(16, 16);
        const auto rejects = admissionRejections(AdmissionStore::Working);
        bool rejected = false;
        try { reconcileWindowCapture(160, 96); }
        catch (const AdmissionError&) { rejected = true; }
        check("selected source difference uses Working rejection", rejected && admissionRejections(AdmissionStore::Working) == rejects + 1);
        check("rejected source preserves existing reservation", workingMemory() == before + limit - small);
    }
    check("rejected source reservation retires", workingMemory() == before);
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
#endif
}
TC_CORE_TEST_MAIN() {
    size_t row = 0, bytes = 0;
    check("source extent layout", internal::captureLayout(160,96,4,row,bytes) && row == 640 && bytes == 61440);
    check("nonpositive source rejected", !internal::captureLayout(0,96,4,row,bytes) && !internal::captureLayout(160,-1,4,row,bytes));
    check("row representation rejected before allocation", !internal::captureLayout(INT_MAX,2,4,row,bytes));
    check("invalid format rejected before allocation", !internal::captureLayout(4,4,0,row,bytes));
#ifndef __EMSCRIPTEN__
    delayedCompletionAccounting(false);
    delayedCompletionAccounting(true);
    sourceExtentAccounting();
    mcp::internal::CaptureCompletionGate inProgress;
    bool finished = false;
    inProgress.complete([&] { inProgress.abandon(); finished = true; });
    check("abandonment does not wait for an in-progress consumer", finished);
    payloadReleaseOrder(false);
    payloadReleaseOrder(true);
    exercise(false,false,false);
    exercise(true,false,false);
    exercise(false,true,false);
    exercise(false,false,true);
    productionWorkerReleaseOrder(0);
    productionWorkerReleaseOrder(1);
    productionWorkerReleaseOrder(2);
    int released = 0;
    auto lease = std::shared_ptr<void>(new int(1), [&](void* p) { ++released; delete static_cast<int*>(p); });
    auto thunk = mcp::internal::carryCaptureAdmission([]() -> std::string { throw std::runtime_error("encode failed"); }, std::move(lease));
    try { thunk(); } catch (const std::runtime_error&) {}
    check("encoder exception releases credit", released == 1);
#endif
    return failures ? 1 : 0;
}
