#pragma once
#include "tcMcpLimits.h"
#include <atomic>
#include <utility>

namespace trussc::mcp::internal {
// Native jobs retain the request's existing budget and optional save slot.
// Extent reconciliation belongs exclusively to reconcileWindowCapture().
class WindowCaptureAdmission {
public:
    struct Ownership {
        std::shared_ptr<WorkBudget> budget;
        std::shared_ptr<void> credit;
    };
    explicit WindowCaptureAdmission(std::shared_ptr<void> credit)
        : previous_(current()), credit_(std::move(credit)) { current() = this; }
    ~WindowCaptureAdmission() { current() = previous_; }
    WindowCaptureAdmission(const WindowCaptureAdmission&) = delete;
    WindowCaptureAdmission& operator=(const WindowCaptureAdmission&) = delete;
    static std::shared_ptr<Ownership> retain() {
        auto budget = currentWorkBudget();
        auto credit = current() ? current()->credit_ : std::shared_ptr<void>{};
        if (!budget && !credit) return {};
        return std::make_shared<Ownership>(Ownership{std::move(budget), std::move(credit)});
    }
private:
    static WindowCaptureAdmission*& current();
    WindowCaptureAdmission* previous_;
    std::shared_ptr<void> credit_;
};

// Native adapters and delayed-completion fixtures share this terminal seam.
// Resource cleanup and consumer destruction precede admission release/signaling.
template<class Job, class Readback, class Cleanup>
void finishCaptureJob(Job& job, const Readback& readback, Cleanup&& cleanup) {
    auto consume = std::move(job.completion);
    try { consume(readback); }
    catch (...) { /* cleanup also runs after a failed consumer */ }
    consume = {};
    cleanup();
    job.admission.reset();
    auto terminal = std::move(job.finished);
    if (terminal) terminal();
}

// Timeout never waits for a consumer already copying its result. A later
// callback skips allocation; an in-progress consumer keeps the job's admission.
class CaptureCompletionGate {
public:
    template<class F> void complete(F&& consume) {
        if (!abandoned_.load()) consume();
    }
    void abandon() { abandoned_.store(true); }
private:
    std::atomic<bool> abandoned_{false};
};
}
