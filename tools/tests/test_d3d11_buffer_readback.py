"""Run the production D3D11 request against a deterministic headless COM mock."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

HEADER = r'''
#pragma once
#include <cstdint>
#include <vector>
using HRESULT = int32_t;
using BOOL = int;
constexpr BOOL FALSE = 0;
constexpr HRESULT S_OK = 0, S_FALSE = 1, E_FAIL = -1;
#define FAILED(status) ((status) < 0)
#define SUCCEEDED(status) ((status) >= 0)
enum { D3D11_USAGE_STAGING = 3, D3D11_CPU_ACCESS_READ = 0x20000,
       D3D11_QUERY_EVENT = 0, D3D11_ASYNC_GETDATA_DONOTFLUSH = 1, D3D11_MAP_READ = 1 };
struct D3D11_BUFFER_DESC {
    unsigned ByteWidth, Usage, BindFlags, CPUAccessFlags, MiscFlags, StructureByteStride;
};
struct D3D11_QUERY_DESC { unsigned Query, MiscFlags; };
struct D3D11_MAPPED_SUBRESOURCE { void* pData; unsigned RowPitch, DepthPitch; };
struct ID3D11Buffer {
    D3D11_BUFFER_DESC desc{};
    std::vector<unsigned char> bytes;
    void GetDesc(D3D11_BUFFER_DESC* outDesc) { *outDesc = desc; }
    void Release();
};
struct ID3D11Query { void Release(); };
struct ID3D11Device {
    HRESULT CreateBuffer(const D3D11_BUFFER_DESC*, const void*, ID3D11Buffer**);
    HRESULT CreateQuery(const D3D11_QUERY_DESC*, ID3D11Query**);
};
struct ID3D11DeviceContext {
    void CopyResource(ID3D11Buffer*, ID3D11Buffer*);
    void End(ID3D11Query*);
    void Flush();
    HRESULT GetData(ID3D11Query*, void*, unsigned, unsigned);
    HRESULT Map(ID3D11Buffer*, unsigned, unsigned, unsigned, D3D11_MAPPED_SUBRESOURCE*);
    void Unmap(ID3D11Buffer*, unsigned);
};
'''

SOURCE = r'''
#include "sokol/sokol_gfx.h"
#include "tc/gpu/internal/tcBufferReadback.h"
#include <d3d11.h>
#include <cassert>
#include <cstring>
#include <string>
ID3D11Device device;
ID3D11DeviceContext context;
std::unique_ptr<ID3D11Buffer> source;
std::vector<std::string> calls;
int buffers = 0, queries = 0, pollsUntilReady = 2;
bool failBuffer = false, failQuery = false, failGetData = false, failMap = false;
bool missingDevice = false, missingContext = false;
const void* sg_d3d11_device() { return missingDevice ? nullptr : &device; }
const void* sg_d3d11_device_context() { return missingContext ? nullptr : &context; }
sg_d3d11_buffer_info sg_d3d11_query_buffer_info(sg_buffer) {
    sg_d3d11_buffer_info info{}; info.buf = source.get(); return info;
}
void ID3D11Buffer::Release() { assert(buffers > 0); --buffers; calls.push_back("release buffer"); delete this; }
void ID3D11Query::Release() { assert(queries > 0); --queries; calls.push_back("release query"); delete this; }
HRESULT ID3D11Device::CreateBuffer(const D3D11_BUFFER_DESC* desc, const void* initial, ID3D11Buffer** outBuffer) {
    calls.push_back("create buffer");
    assert(!initial && desc->ByteWidth == 4 && desc->Usage == D3D11_USAGE_STAGING);
    assert(!desc->BindFlags && desc->CPUAccessFlags == D3D11_CPU_ACCESS_READ);
    assert(!desc->MiscFlags && !desc->StructureByteStride);
    if (failBuffer) return E_FAIL;
    *outBuffer = new ID3D11Buffer{*desc, std::vector<unsigned char>(desc->ByteWidth)};
    ++buffers; return S_OK;
}
HRESULT ID3D11Device::CreateQuery(const D3D11_QUERY_DESC* desc, ID3D11Query** outQuery) {
    calls.push_back("create query");
    assert(desc->Query == D3D11_QUERY_EVENT && !desc->MiscFlags);
    if (failQuery) return E_FAIL;
    *outQuery = new ID3D11Query; ++queries; return S_OK;
}
void ID3D11DeviceContext::CopyResource(ID3D11Buffer* target, ID3D11Buffer* input) {
    assert(target != input && target->bytes.size() == input->bytes.size());
    calls.push_back("copy"); target->bytes = input->bytes;
}
void ID3D11DeviceContext::End(ID3D11Query* query) { assert(query); calls.push_back("end query"); }
void ID3D11DeviceContext::Flush() { calls.push_back("flush"); }
HRESULT ID3D11DeviceContext::GetData(ID3D11Query* query, void* data, unsigned size, unsigned flags) {
    assert(query && size == sizeof(BOOL) && flags == D3D11_ASYNC_GETDATA_DONOTFLUSH);
    calls.push_back("poll");
    if (failGetData) return E_FAIL;
    if (pollsUntilReady > 0) { --pollsUntilReady; return S_FALSE; }
    *static_cast<BOOL*>(data) = 1; return S_OK;
}
HRESULT ID3D11DeviceContext::Map(ID3D11Buffer* buffer, unsigned subresource, unsigned mode,
                                unsigned flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
    assert(!subresource && mode == D3D11_MAP_READ && !flags && pollsUntilReady == 0);
    calls.push_back("map");
    if (failMap) return E_FAIL;
    mapped->pData = buffer->bytes.data(); return S_OK;
}
void ID3D11DeviceContext::Unmap(ID3D11Buffer* buffer, unsigned subresource) {
    assert(buffer && !subresource && !failMap); calls.push_back("unmap");
}
namespace trussc {
class StorageBuffer {
public:
    sg_buffer buffer_{};
    size_t size_ = 4;
    std::unique_ptr<internal::BufferReadbackRequest> readAsyncPlatform();
};
}
#include "core/platform/win/tcStorageBufferReadback.h"
void reset() {
    assert(buffers == 0 && queries == 0);
    calls.clear(); pollsUntilReady = 2;
    source = std::make_unique<ID3D11Buffer>();
    source->desc = {4, 0, 0x80, 0, 0x40, 4};
    source->bytes = {1, 2, 3, 4};
}
int main() {
    using namespace trussc;
    using namespace trussc::internal;
    BufferReadbackQueue queue;
    StorageBuffer buffer;
    ReadbackResult retained;
    reset();
    auto request = buffer.readAsyncPlatform();
    assert((calls == std::vector<std::string>{"create buffer", "create query", "copy", "end query", "flush"}));
    source->bytes = {9, 9, 9, 9}; source.reset(); // No borrowed source survives the copy.
    int callbacks = 0;
    request->callback = [&](ReadbackResult result) {
        assert(buffers == 0 && queries == 0); // Released before entering user code.
        retained = std::move(result); ++callbacks;
    };
    queue.enqueue(std::move(request));
    queue.poll(); queue.poll();
    assert(callbacks == 0 && calls.back() == "poll" && buffers == 1 && queries == 1);
    queue.poll();
    assert(callbacks == 1 && retained.ok() && retained.size() == 4);
    assert(std::memcmp(retained.getData(), "\1\2\3\4", 4) == 0);
    assert((calls == std::vector<std::string>{"create buffer", "create query", "copy", "end query", "flush",
        "poll", "poll", "poll", "map", "unmap", "release query", "release buffer"}));

    // GetData/device removal and Map failures finish with !ok() and release both handles.
    for (bool mapFailure : {false, true}) {
        reset(); pollsUntilReady = 0; failMap = mapFailure; failGetData = !mapFailure;
        auto failed = buffer.readAsyncPlatform();
        failed->callback = [&](ReadbackResult result) { assert(!result.ok() && !result.size()); ++callbacks; };
        queue.enqueue(std::move(failed)); queue.poll();
        assert(buffers == 0 && queries == 0);
        failMap = failGetData = false;
    }
    assert(callbacks == 3);

    // Creation failures clean up partially created resources; shared code defers !ok().
    for (bool queryFailure : {false, true}) {
        reset(); failQuery = queryFailure; failBuffer = !queryFailure;
        assert(!buffer.readAsyncPlatform());
        assert(buffers == 0 && queries == 0);
        failQuery = failBuffer = false;
    }
    reset();
    missingDevice = true; assert(!buffer.readAsyncPlatform()); missingDevice = false;
    missingContext = true; assert(!buffer.readAsyncPlatform()); missingContext = false;
    source.reset(); assert(!buffer.readAsyncPlatform());

    // App end drops captures immediately, then polling retires GPU resources.
    reset();
    auto owner = std::make_shared<AppOwner>();
    auto capture = std::make_shared<int>(42);
    std::weak_ptr<int> weakCapture = capture;
    auto canceled = buffer.readAsyncPlatform();
    canceled->owner = owner;
    canceled->callback = [capture](ReadbackResult) { assert(false); };
    capture.reset(); queue.enqueue(std::move(canceled));
    queue.cancelAppCallbacks(owner);
    assert(weakCapture.expired() && buffers == 1 && queries == 1);
    queue.poll(); queue.poll(); queue.poll();
    assert(buffers == 0 && queries == 0 && callbacks == 3);

    // Dropping a request or shutting down before completion releases both COM handles.
    reset();
    auto dropped = buffer.readAsyncPlatform();
    dropped.reset();
    assert(buffers == 0 && queries == 0);
    reset();
    auto pending = buffer.readAsyncPlatform();
    pending->callback = [](ReadbackResult) { assert(false); };
    queue.enqueue(std::move(pending)); queue.poll(); queue.shutdown();
    assert(buffers == 0 && queries == 0 && callbacks == 3);
    assert(retained.ok() && retained.getData()[0] == 1); // Owned bytes outlive requests.
}
'''


class D3D11BufferReadbackTests(unittest.TestCase):
    def test_snapshot_poll_failures_and_lifetime(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as directory:
            folder = Path(directory)
            (folder / 'd3d11.h').write_text(HEADER, encoding='utf-8')
            code = folder / 'readback.cpp'
            code.write_text(SOURCE, encoding='utf-8')
            binary = folder / 'readback'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g',
                            '-I', str(folder), '-I', str(ROOT / 'core/include'), '-I', str(ROOT),
                            str(code), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
