#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <istream>
#include <string_view>

namespace trussc::internal {

// A bounded, lazy buffer. The allocator seam tests retained stdin limits
// and whole-line rejection on allocation failure.
class InputStorage {
public:
    using Reallocate = void* (*)(void*, size_t);
    explicit InputStorage(Reallocate allocate = std::realloc) : allocate_(allocate) {}
    ~InputStorage() { std::free(data_); }
    InputStorage(const InputStorage&) = delete;
    InputStorage& operator=(const InputStorage&) = delete;
    bool reserve(size_t needed, size_t limit) {
        if (needed > limit) return false;
        if (needed <= capacity_) return true;
        size_t grown = capacity_ ? capacity_ : (std::min)(size_t{4096}, limit);
        while (grown < needed) grown = grown > limit / 2 ? limit : grown * 2;
        void* replacement = allocate_(data_, grown);
        if (!replacement) return false;
        data_ = static_cast<char*>(replacement);
        capacity_ = grown;
        return true;
    }
    char* data() { return data_; }
    const char* data() const { return data_; }
    size_t capacity() const { return capacity_; }
private:
    Reallocate allocate_;
    char* data_ = nullptr;
    size_t capacity_ = 0;
};

// Report rejection at the first excess byte, even without a newline/EOF.
// Continue draining that entire line; never deliver its retained prefix.
// A byte budget also lets callers bound finite-file work if a file grows.
template<class Line, class Reject>
void readBoundedLines(std::istream& input, size_t lineLimit, size_t byteLimit,
                      Line onLine, Reject onReject,
                      InputStorage::Reallocate allocate = std::realloc) {
    InputStorage storage(allocate);
    size_t length = 0;
    size_t consumed = 0;
    bool rejected = false;
    for (;;) {
        const auto value = input.get();
        if (std::char_traits<char>::eq_int_type(value, std::char_traits<char>::eof())) break;
        if (consumed == byteLimit) { onReject(true); return; }
        ++consumed;
        const char character = std::char_traits<char>::to_char_type(value);
        if (character == '\n') {
            if (!rejected) onLine(std::string_view(storage.data() ? storage.data() : "", length));
            length = 0;
            rejected = false;
        } else if (!rejected) {
            if (length == lineLimit || !storage.reserve(length + 1, lineLimit)) {
                rejected = true;
                onReject(false);
            } else {
                storage.data()[length++] = character;
            }
        }
    }
    if (length && !rejected) onLine(std::string_view(storage.data(), length));
}

} // namespace trussc::internal
