#include <atomic>
#include <barrier>
#include <cstdio>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

// Load the standard headers before hiding the feature macro, so the shim's
// includes cannot restore it. This executable does not link native TrussC.
#ifdef TC_TEST_FORCE_SHARED_PTR_FALLBACK
#undef __cpp_lib_atomic_shared_ptr
#endif
#include "../../include/tc/utils/tcAtomicSharedPtr.h"

namespace {
using trussc::internal::AtomicSharedPtr;
using trussc::internal::sharedLoad;
using trussc::internal::sharedStore;

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
static_assert(std::is_same_v<AtomicSharedPtr<int>, std::atomic<std::shared_ptr<int>>>);
#else
static_assert(!std::is_same_v<AtomicSharedPtr<int>, std::shared_ptr<int>>);
#endif
static_assert(!std::is_copy_constructible_v<AtomicSharedPtr<int>>);
static_assert(!std::is_move_constructible_v<AtomicSharedPtr<int>>);

int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

void snapshotLifetime() {
    AtomicSharedPtr<const int> slot;
    const auto& readable = slot;
    check("default value is null", !sharedLoad(readable));

    auto first = std::make_shared<const int>(42);
    std::weak_ptr<const int> old = first;
    sharedStore(slot, std::move(first));
    auto snapshot = sharedLoad(readable);
    sharedStore(slot, std::make_shared<const int>(84));
    check("replacement preserves an acquired snapshot", snapshot && *snapshot == 42 && !old.expired());
    check("subsequent load sees replacement", *sharedLoad(readable) == 84);
    snapshot.reset();
    check("last snapshot releases the old value", old.expired());

    auto current = sharedLoad(readable);
    std::weak_ptr<const int> last = current;
    current.reset();
    sharedStore(slot, std::shared_ptr<const int>{});
    check("null store releases the current value", !sharedLoad(readable) && last.expired());

    auto initial = std::make_shared<int>(7);
    AtomicSharedPtr<int> initialized(initial);
    check("construction retains initial value", sharedLoad(initialized) == initial);
}

void reentrantDeleter() {
    AtomicSharedPtr<int> slot;
    bool deleted = false;
    bool sawReplacement = false;
    auto value = std::shared_ptr<int>(new int(1), [&](int* p) {
        deleted = true;
        auto current = sharedLoad(slot);
        sawReplacement = current && *current == 2;
        sharedStore(slot, std::make_shared<int>(3));
        delete p;
    });
    sharedStore(slot, std::move(value));
    sharedStore(slot, std::make_shared<int>(2));
    check("deleter runs after unlocking and sees replacement", deleted && sawReplacement);
    check("deleter can store into the same slot", *sharedLoad(slot) == 3);
}

struct Snapshot {
    int sequence;
    int inverse;
    std::atomic<int>& destroyed;

    Snapshot(int value, std::atomic<int>& count)
        : sequence(value), inverse(-value), destroyed(count) {}
    ~Snapshot() { destroyed.fetch_add(1, std::memory_order_relaxed); }
};

void concurrentPublication() {
    constexpr int iterations = 10000;
    constexpr int writers = 2;
    constexpr int readers = 2;
    std::atomic<int> destroyed{0};
    std::atomic<int> invalid{0};
    AtomicSharedPtr<const Snapshot> slot;
    std::barrier start(writers + readers);
    std::vector<std::thread> workers;
    for (int writer = 0; writer < writers; ++writer) {
        workers.emplace_back([&, writer] {
            std::shared_ptr<const Snapshot> first =
                std::make_shared<Snapshot>(writer * iterations + 1, destroyed);
            sharedStore(slot, std::move(first));
            start.arrive_and_wait();
            for (int i = 2; i <= iterations; ++i) {
                std::shared_ptr<const Snapshot> next =
                    std::make_shared<Snapshot>(writer * iterations + i, destroyed);
                sharedStore(slot, std::move(next));
            }
        });
    }
    for (int reader = 0; reader < readers; ++reader) {
        workers.emplace_back([&] {
            start.arrive_and_wait();
            for (int i = 0; i < iterations; ++i) {
                auto snapshot = sharedLoad(slot);
                if (!snapshot || snapshot->sequence <= 0 ||
                    snapshot->sequence > writers * iterations ||
                    snapshot->inverse != -snapshot->sequence) {
                    invalid.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    check("concurrent readers see fully initialized snapshots", invalid.load() == 0);
    check("concurrent writers retain exactly one final snapshot",
          sharedLoad(slot) && destroyed.load() == writers * iterations - 1);
    sharedStore(slot, std::shared_ptr<const Snapshot>{});
    check("every published snapshot is destroyed exactly once",
          destroyed.load() == writers * iterations);
}
} // namespace

int main() {
#ifdef TC_TEST_FORCE_SHARED_PTR_FALLBACK
    std::puts("Forced lock-based fallback");
#else
    std::puts("Library-selected implementation");
#endif
    snapshotLifetime();
    reentrantDeleter();
    concurrentPublication();
    return failures ? 1 : 0;
}
