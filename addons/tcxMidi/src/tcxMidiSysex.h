#pragma once

#include "tc/utils/tcMainThread.h"
#include "tc/utils/tcLog.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace tcx::midi::internal {
// Internal diagnostics for SysEx assembly and backend receive losses, independent of polling
// queue losses. The owner retains this across port close/reopen.
struct SysexLoss : tc::internal::MainThreadSlot, std::enable_shared_from_this<SysexLoss> {
    std::atomic<uint64_t> discarded{0};

    static std::shared_ptr<SysexLoss> create() {
        return std::shared_ptr<SysexLoss>(new SysexLoss);
    }

    void count(size_t needed) noexcept {
        // needed == 0 denotes a backend loss with unknown size (WinMM).
        // The backend records in pre-existing state and posts the embedded
        // slot: no closure, queue lock, allocation or logger call here.
        if (discarded.fetch_add(1, std::memory_order_relaxed) == 0) {
            firstNeeded_ = needed;
            pending_.store(true, std::memory_order_release);
#if !defined(__EMSCRIPTEN__)
            // Nothing is queued while idle. The slot keeps this state alive
            // through close/reopen or owner destruction until its sole report.
            post(weak_from_this().lock());
#endif
        }
    }

private:
    SysexLoss() : MainThreadSlot(&deliver) {}
    size_t firstNeeded_ = 0;
    std::atomic<bool> pending_{false};

    static void deliver(tc::internal::MainThreadSlot& slot) {
        auto& self = static_cast<SysexLoss&>(slot);
        if (self.pending_.exchange(false, std::memory_order_acquire)) {
            if (self.firstNeeded_ == 0) {
                tc::logWarning("tcxMidiIn") << "SysEx assembly: receive loss (WinMM long-message "
                    "error or buffer re-queue failure; size unknown); SysEx loss count "
                    << self.discarded.load(std::memory_order_relaxed);
                return;
            }
            tc::logWarning("tcxMidiIn") << "SysEx assembly: discarded a message (limit "
                << 1048576 << " bytes including F0/F7, needed " << self.firstNeeded_
                << " bytes); SysEx loss count "
                << self.discarded.load(std::memory_order_relaxed);
        }
    }
};
} // namespace tcx::midi::internal
