#include "tcBoundedFontFetch.h"
#ifdef __EMSCRIPTEN__
#include "TrussC.h"
#include <emscripten.h>
#include <cstdlib>

namespace trussc::internal {
namespace {
size_t rejectedFontFetches = 0; // browser main thread, owning core lifetime

// Default readers work on all Fetch stream implementations. Copy each ingress
// view immediately into packed blocks: short reads never retain its backing.
EM_JS_DEPS(fontStreamDeps, "$UTF8ToString,$getWasmTableEntry,malloc,free");
EM_JS(void, startFontStream, (const char* url, void* request, unsigned timeout, size_t limit, void* done), {
    const registry = Module['tcFontStreams'] || (Module['tcFontStreams'] = new Map());
    const entry = { controller: new AbortController(), chunks: [], reader: null, capacity: 0, credits: 0 };
    registry.set(request, entry);
    const address = UTF8ToString(url);
    const timer = setTimeout(() => entry.controller.abort(), timeout);
    (async () => {
        let buffer = 0, size = 0, status = 0;
        try {
            const response = await fetch(address, {signal: entry.controller.signal});
            if (!response.ok || !response.body) throw new Error('font fetch failed');
            const length = response.headers.get('Content-Length');
            if (length !== null && Number(length) > limit) {
                status = -1; await response.body.cancel(); throw new Error('font limit');
            }
            entry.reader = response.body.getReader();
            const blockSize = 65536, bookkeeping = 1024;
            // Charge full backing capacity and conservative per-block/view/list
            // bookkeeping credits before allocation, independently of payload.
            const storageLimit = Math.ceil(limit / blockSize) * (blockSize + bookkeeping);
            for (;;) {
                const chunk = await entry.reader.read();
                if (registry.get(request) !== entry) return;
                if (chunk.value && chunk.value.byteLength) {
                    if (chunk.value.byteLength > limit - size) { status = -1; throw new Error('font limit'); }
                    let consumed = 0;
                    while (consumed < chunk.value.byteLength) {
                        if (size === entry.capacity) {
                            if (blockSize + bookkeeping > storageLimit - entry.credits) {
                                status = -1; throw new Error('font storage limit');
                            }
                            entry.credits += blockSize + bookkeeping;
                            entry.capacity += blockSize;
                            entry.chunks.push(new Uint8Array(blockSize));
                        }
                        const offset = size % blockSize;
                        const count = Math.min(chunk.value.byteLength - consumed, blockSize - offset);
                        entry.chunks[entry.chunks.length - 1].set(chunk.value.subarray(consumed, consumed + count), offset);
                        size += count;
                        consumed += count;
                    }
                }
                if (chunk.done) break;
            }
            buffer = _malloc(size || 1);
            if (!buffer) throw new Error('font allocation failed');
            let offset = buffer;
            for (const chunk of entry.chunks) {
                const count = Math.min(chunk.byteLength, buffer + size - offset);
                HEAPU8.set(chunk.subarray(0, count), offset); offset += count;
            }
            status = response.status;
        } catch (_) {
            if (buffer) { _free(buffer); buffer = 0; }
            size = 0;
        } finally {
            clearTimeout(timer);
            entry.chunks.length = 0;
            entry.capacity = entry.credits = 0;
            if (entry.reader) { try { await entry.reader.cancel(); } catch (_) {} }
            entry.controller.abort();
            if (registry.get(request) === entry) {
                registry.delete(request);
                // The C callback owns buffer from here (and may close request).
                getWasmTableEntry(done)(request, buffer, size, status);
            } else if (buffer) { _free(buffer); }
        }
    })();
});
EM_JS(void, cancelFontStream, (void* request), {
    const registry = Module['tcFontStreams'];
    const entry = registry && registry.get(request);
    if (!entry) return;
    registry.delete(request);
    entry.chunks.length = 0;
    entry.capacity = entry.credits = 0;
    entry.controller.abort();
    if (entry.reader) entry.reader.cancel().catch(() => {});
});

void finishFontStream(BoundedFontFetch* request, char* data, size_t size, int status) {
    request->data = data;
    request->numBytes = size;
    request->status = status;
    // Completion detaches AsyncState::fetch and owns the callback holder.
    // Defer close/delete until it returns, then log with no live owner edge.
    const bool warn = status == -1 && rejectedFontFetches++ == 0;
    auto complete = request->complete;
    request->complete = nullptr;
    request->completing = true;
    if (complete) complete(request, status >= 200 && status < 300);
    request->completing = false;
    if (request->closePending) closeBoundedFontFetch(request);
    if (warn) {
        logWarning("Font") << "URL font rejected: capacity " << fontFetchByteLimit
            << ", needed at least " << fontFetchByteLimit + 1 << " bytes (one whole fetch lost)";
    }
}
} // namespace

BoundedFontFetch* startBoundedFontFetch(const char* url, void* userData,
    unsigned timeout, void (*complete)(BoundedFontFetch*, bool)) {
    auto* request = new BoundedFontFetch;
    request->userData = userData;
    request->complete = complete;
    startFontStream(url, request, timeout, fontFetchByteLimit, reinterpret_cast<void*>(finishFontStream));
    return request;
}
void closeBoundedFontFetch(BoundedFontFetch* request) {
    if (!request) return;
    if (request->completing) { request->closePending = true; return; }
    cancelFontStream(request);
    std::free(const_cast<char*>(request->data));
    delete request;
}
size_t fontFetchLossCountForTests() { return rejectedFontFetches; }
} // namespace trussc::internal
#endif
