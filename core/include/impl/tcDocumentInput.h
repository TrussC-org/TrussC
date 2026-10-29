#pragma once
#include <algorithm>
#include <cstdint>
#include <istream>
#include <limits>
#include <string>

namespace trussc::internal {
inline constexpr size_t unknownDocumentSizeLimit = 64u * 1024u * 1024u;
// The explicit storage ceiling lets unit tests exercise the string limit
// without allocating a platform-sized string. Production uses max_size().
inline bool readDocumentBytesToEnd(std::istream& input, std::string& bytes,
                                   size_t storageLimit, const char** error = nullptr) {
    if (error) *error = nullptr;
    std::string complete;
    storageLimit = std::min(storageLimit, complete.max_size());
    // Bound (security): only the unknown-size fallback is capped at 64 MiB, with one extra byte probed before rejection.
    storageLimit = std::min(storageLimit, unknownDocumentSizeLimit);
    char chunk[8192];
    for (;;) {
        // Probe one byte at the ceiling so exactly-full input can still succeed.
        const size_t count = complete.size() == storageLimit ? 1 :
            std::min(sizeof(chunk), storageLimit - complete.size());
        input.read(chunk, std::streamsize(count));
        const size_t received = size_t(input.gcount());
        // Bound (abi): actual bytes must fit the same string storage limit as sized documents. Overflow rejects the whole document through the caller's existing read error.
        if (received > storageLimit - complete.size()) {
            if (error && storageLimit == unknownDocumentSizeLimit)
                *error = "document size unknown and over 64 MiB";
            return false;
        }
        complete.append(chunk, received);
        if (input.bad() || (input.fail() && !input.eof())) return false;
        if (input.eof()) {
            bytes.swap(complete);
            return true;
        }
    }
}

// Call only after the loader's regular-file check, with a fresh binary stream.
// Compare usable sizes BEFORE reading: libc++ and MSVC filebuf may expose a
// read error as ordinary EOF. Size-zero or unseekable regular files need EOF.
inline bool readDocumentBytes(std::istream& input, std::string& bytes,
                              const char** error = nullptr) {
    if (error) *error = nullptr;
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end <= 0) {
        input.clear();
        input.seekg(0, std::ios::beg);
        // Some regular pseudo-files cannot seek at all; the fresh stream is
        // still at its initial read position after the failed seeks.
        input.clear();
        return readDocumentBytesToEnd(input, bytes, bytes.max_size(), error);
    }
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
