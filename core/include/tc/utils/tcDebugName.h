#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace trussc::internal {
// Process-wide debug registry, implemented in the core library for hot reload.
// GPU objects and MCP queries remain main-thread owned.
enum class DebugObjectKind { Fbo, Image };
struct DebugObjectEntry {
    uint64_t index;
    void* object;
    std::string name;
};
class DebugName {
public:
    DebugName(DebugObjectKind kind, void* object);
    DebugName(DebugName&& other, void* object) noexcept;
    ~DebugName();
    DebugName(const DebugName&) = delete;
    DebugName& operator=(const DebugName&) = delete;
    void moveFrom(DebugName& other) noexcept;
    void set(const std::string& name);
    const std::string& get() const { return name_; }
private:
    DebugObjectKind kind_;
    void* object_;
    uint64_t index_ = 0;
    std::string name_;
};
std::vector<DebugObjectEntry> debugObjects(DebugObjectKind kind);
}
