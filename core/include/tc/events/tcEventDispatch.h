#pragma once
#include "../app/internal/tcAppConstruction.h"

namespace trussc::internal {
// No owner-run batching: only the entry owner can bypass AppCallbackScope.
struct EventDispatch {
    EventDispatchContext context;
    bool captured = false;
    EventDispatch() {
        if (currentAppOwner()) capture();
    }
    void capture() {
        if (captured) return;
        context = captureEventDispatchContext();
        captured = true;
    }
    bool mainThread() {
        capture();
        return context.mainThread;
    }
    struct Restore {
        const EventDispatchContext& context;
        WindowContext* expected;
        ~Restore() {
            if (context.selected && *context.selected != expected) repairEventContext(expected);
        }
    };
    template<class Entry, class Invoke> void invoke(const Entry& entry, Invoke&& call) {
        // Unowned Inline notifications must not seed the main-thread identity.
        if (entry.owner) capture();
        if (!captured || entry.frameworkOwned || entry.owner.get() == context.eligibleOwner) {
            Restore restore{context, context.original};
            call();
        } else {
            AppCallbackScope scope(entry.owner);
            if (!scope.admitted()) return;
            Restore restore{context, context.selected ? *context.selected : nullptr};
            call();
        }
    }
    template<class Entry, class Invoke> void invokeQueued(const Entry& entry, Invoke&& call) {
        if (entry.owner && !entry.owner->token->load() &&
            !(entry.cancellation->exitEvent && entry.owner->ended)) return;
        invoke(entry, std::forward<Invoke>(call));
    }
};
} // namespace trussc::internal
