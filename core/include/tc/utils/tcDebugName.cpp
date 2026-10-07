#include "tcDebugName.h"
#include <map>
#include <mutex>
#include <utility>

namespace trussc::internal {
namespace {
struct Entry { DebugObjectKind kind; DebugName* debug; void* object; };
struct Registry {
    std::mutex mutex;
    uint64_t next = 1;
    std::map<uint64_t, Entry> entries;
};
Registry& registry() { static Registry r; return r; }
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
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    std::vector<DebugObjectEntry> result;
    for (const auto& [index, e] : r.entries)
        if (e.kind == kind) result.push_back({index, e.object, e.debug->get()});
    return result;
}
}
