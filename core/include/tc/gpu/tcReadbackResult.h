#pragma once
#include "../utils/tcAnnotations.h"
#include "../utils/tcLog.h"
#include <cstddef>
#include <cstring>
#include <type_traits>
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
    template <class T> T get(size_t index) const {
        static_assert(std::is_trivially_copyable_v<T>, "ReadbackResult::get requires a trivially copyable type");
        // Bound (copy range): the complete typed element must fit the owned snapshot.
        if (index >= size() / sizeof(T)) {
            logWarning("ReadbackResult") << "get index is out of range";
            return T{};
        }
        T value{};
        std::memcpy(&value, getData() + index * sizeof(T), sizeof(T));
        return value;
    }
private:
    std::vector<unsigned char> bytes_;
    bool ok_ = false;
};
} // namespace trussc
