#include "tcDebugName.h"
#include <atomic>
#include <map>
#include <mutex>
#include <type_traits>
#include <utility>

namespace trussc::internal {
bool frameworkShutdown() noexcept;
namespace {
// The registry can die before a user global that owns an Fbo or Image. The
// marker closes it first; the constant-initialized atomic outlives both.
constinit std::atomic<bool> debugRegistryDestroyed{false};
static_assert(std::is_trivially_destructible_v<decltype(debugRegistryDestroyed)>);
struct RegistryLifetimeMark {
    ~RegistryLifetimeMark() { debugRegistryDestroyed.store(true, std::memory_order_release); }
};
struct Entry { DebugObjectKind kind; DebugName* debug; void* object; };
struct Registry {
    std::mutex mutex;
    uint64_t next = 1;
    std::map<uint64_t, Entry> entries;
};
Registry& registry() {
    static Registry r;
    static RegistryLifetimeMark lifetime;
    (void)lifetime;
    return r;
}
}
DebugName::DebugName(DebugObjectKind kind, void* object) : kind_(kind), object_(object) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    index_ = r.next++;
    r.entries.emplace(index_, Entry{kind_, this, object_});
}
DebugName::DebugName(DebugName&& other, void* object) noexcept
    : kind_(other.kind_), object_(object) { moveFrom(other); }
DebugName::~DebugName() {
    // Runner shutdown leaves this CPU registry alive for the next run.
    if (debugRegistryDestroyed.load(std::memory_order_acquire)) return;
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    r.entries.erase(index_);
}
void DebugName::moveFrom(DebugName& other) noexcept {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    r.entries.erase(index_);
    index_ = std::exchange(other.index_, 0);
    name_ = std::move(other.name_);
    other.name_.clear();
    if (index_) r.entries.at(index_) = Entry{kind_, this, object_};
}
void DebugName::set(const std::string& name) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    name_ = name;
    if (!index_) {
        index_ = r.next++;
        r.entries.emplace(index_, Entry{kind_, this, object_});
    }
}
std::vector<DebugObjectEntry> debugObjects(DebugObjectKind kind) {
    if (frameworkShutdown() || debugRegistryDestroyed.load(std::memory_order_acquire)) return {};
    auto& r = registry();
    std::unique_lock lock(r.mutex);
    size_t count = 0;
    for (const auto& [index, e] : r.entries)
        if (e.kind == kind) ++count;
    std::vector<DebugObjectEntry> result;
    result.reserve(count);
    for (const auto& [index, e] : r.entries)
        if (e.kind == kind) result.push_back({index, e.object, e.debug->get()});
    return result;
}
}
