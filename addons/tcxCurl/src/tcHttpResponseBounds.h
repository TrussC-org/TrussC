#pragma once

// Internal admission and diagnostics; not a public capacity/statistics API.
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace tcx::curl::detail {
// Bound (security): peers control cumulative request body size across redirects, including decoded expansion. Overflow rejects the whole response and is counted.
constexpr size_t responseByteLimit = 64 * 1024 * 1024;
// Bound (security): peers control headers, redirects and error text. Overflow rejects the whole response and is counted.
constexpr size_t metadataByteLimit = 64 * 1024;
// Bound (security): small JSON can amplify into a large DOM. Overflow rejects the whole parse and is counted.
constexpr size_t jsonByteLimit = 16 * 1024 * 1024;
// Bound (security): nested peer JSON can exhaust parser/traversal stacks. Overflow rejects the whole parse and is counted.
constexpr size_t jsonDepthLimit = 64;
// Bound (security): tiny peer JSON nodes can amplify DOM storage. Overflow rejects the whole parse and is counted.
constexpr size_t jsonNodeLimit = 1000000;
// Bound (security): DOM, lexer and result copies share finite working storage. Overflow rejects the whole parse and is counted.
constexpr size_t jsonWorkingLimit = 128 * 1024 * 1024;

enum class ResponseLoss { Body, Metadata, Json, BackendHeader, HeaderSyntax, Redirects, RedirectPolicy, Count };
void reportResponseLoss(ResponseLoss kind, size_t needed, size_t capacity, const char* units = "bytes");
size_t responseLossCountForTests(ResponseLoss kind);
void reportInvalidResponseLimit();
// Each client instance owns its Warning lifetime, including copied clients.
// Assignment changes settings, while preserving the destination's lifetime.
struct RequestHeaderWarning {
    std::atomic<bool> issued{false};
    RequestHeaderWarning() = default;
    RequestHeaderWarning(const RequestHeaderWarning&) noexcept {}
    RequestHeaderWarning& operator=(const RequestHeaderWarning&) noexcept { return *this; }
};
void reportInvalidRequestHeader(RequestHeaderWarning& warning, const char* reason);
nlohmann::json parseResponseJson(const std::string& body);

// Fixed blocks arrive on demand. Finalization reserves only the received length
// and releases each block after copying; initialized payload stays near one body.
struct ResponseBuffer {
    static constexpr size_t blockSize = 64 * 1024;
    struct Block { std::unique_ptr<Block> next; char bytes[blockSize]; };
    std::unique_ptr<Block> first;
    Block* last = nullptr;
    size_t length = 0, capacity = 0, metadata = 0, needed = 0;
    size_t total = 0, wireTotal = 0, hopWire = 0; // survive disposable per-hop storage
    size_t limit;
    bool rejected = false, expectStatus = true, headersEnded = false, bodyStarted = false;
    bool verbose = false, backendHeaderLimit = false, requestIssued = false;
    int status = 0;
    void* easy = nullptr;
    void* verboseState = nullptr;
    char backendError[256] = {};
    ResponseLoss loss = ResponseLoss::Body;

    explicit ResponseBuffer(size_t bytes = responseByteLimit) : limit(bytes) {}
    ~ResponseBuffer() { clear(); }
    void clear() {
        // Iterative destruction avoids a block-count-dependent call stack.
        while (first) { auto next = std::move(first->next); first = std::move(next); }
        last = nullptr;
        length = capacity = 0;
        bodyStarted = false;
    }
    bool reject(ResponseLoss reason, size_t amount) {
        if (!rejected) { rejected = true; loss = reason; needed = amount; }
        clear();
        return false;
    }
    bool append(const char* data, size_t count) noexcept {
        if (rejected) return false;
        if (count > limit - total) return reject(ResponseLoss::Body, limit == SIZE_MAX ? limit : limit + 1);
        total += count;
        if (count) bodyStarted = true;
        try {
            while (count) {
                if (length == capacity) {
                    auto block = std::make_unique<Block>();
                    auto* tail = block.get();
                    if (last) last->next = std::move(block); else first = std::move(block);
                    last = tail;
                    capacity += blockSize;
                }
                const size_t offset = length % blockSize;
                const size_t part = (std::min)(count, blockSize - offset);
                std::memcpy(last->bytes + offset, data, part);
                length += part; data += part; count -= part;
            }
            return true;
        } catch (...) { return reject(ResponseLoss::Body, length); }
    }
    void publish(std::string& result) {
        result.reserve(length); // never reserve from Content-Length
        size_t remaining = length;
        while (first) {
            const size_t part = (std::min)(remaining, blockSize);
            result.append(first->bytes, part);
            remaining -= part;
            auto next = std::move(first->next);
            first = std::move(next);
        }
        last = nullptr; length = capacity = 0;
    }
    bool header(const char* data, size_t count) noexcept {
        if (rejected) return false;
        if (count > metadataByteLimit - metadata)
            return reject(ResponseLoss::Metadata, metadataByteLimit + 1);
        metadata += count; // includes interim responses, redirects and trailers
        // Response boundaries come from libcurl request/interim state, never a
        // text prefix. Status-like fields after the header block are ambiguous.
        if (headersEnded && count >= 5 && std::memcmp(data, "HTTP/", 5) == 0 &&
            count > 5 && data[5] >= '0' && data[5] <= '9')
            return reject(ResponseLoss::HeaderSyntax, metadata);
        return true;
    }
    void report() const {
        if (backendHeaderLimit)
            reportResponseLoss(ResponseLoss::BackendHeader, 1, 0, "backend header limit");
        else if (rejected) reportResponseLoss(loss, needed,
            loss == ResponseLoss::Metadata ? metadataByteLimit : loss == ResponseLoss::Redirects ? 5 :
            loss == ResponseLoss::RedirectPolicy ? 0 : limit,
            loss == ResponseLoss::Redirects ? "redirects" : loss == ResponseLoss::RedirectPolicy ? "targets" : "bytes");
    }
};
} // namespace tcx::curl::detail
