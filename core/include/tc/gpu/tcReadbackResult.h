#pragma once
#include "../utils/tcAnnotations.h"
#include <cstddef>
#include <vector>
#include <utility>

namespace trussc {
// An owned CPU snapshot. Failure has no bytes.
class TC_LUA_SKIP ReadbackResult {
public:
    ReadbackResult() = default;
    explicit ReadbackResult(std::vector<unsigned char> bytes)
        : bytes_(std::move(bytes)), ok_(true) {}
    bool ok() const { return ok_; }
    size_t size() const { return bytes_.size(); }
    const unsigned char* getData() const { return bytes_.data(); }
private:
    std::vector<unsigned char> bytes_;
    bool ok_ = false;
};
} // namespace trussc
