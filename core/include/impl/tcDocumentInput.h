#pragma once
#include <algorithm>
#include <cstdint>
#include <istream>
#include <limits>
#include <string>

namespace trussc::internal {
// Call with a binary, seekable local file. Compare against its size BEFORE
// reading: libc++ and MSVC filebuf may expose a read error as ordinary EOF.
inline bool readDocumentBytes(std::istream& input, std::string& bytes) {
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) return false;
    std::string complete;
    // Bound (abi): the measured byte count must fit string storage before resize. Overflow rejects the whole document and is counted.
    if (uintmax_t(end) > complete.max_size()) return false;
    input.seekg(0, std::ios::beg);
    if (!input) return false;
    complete.resize(size_t(end));
    size_t offset = 0;
    while (offset < complete.size()) {
        // Bound (abi): each read and its actual byte count must fit streamsize; larger totals use multiple reads. Overflow in storage rejects the whole document and is counted.
        const size_t count = std::min(complete.size() - offset,
                                     size_t(std::numeric_limits<std::streamsize>::max()));
        input.read(complete.data() + offset, std::streamsize(count));
        if (input.gcount() != std::streamsize(count) || !input) return false;
        offset += count;
    }
    // A growing file must not silently lose its new tail either.
    if (input.peek() != std::char_traits<char>::eof() || input.bad()) return false;
    bytes.swap(complete);
    return true;
}
}
