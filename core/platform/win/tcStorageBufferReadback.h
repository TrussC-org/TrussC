#pragma once
#include "tc/gpu/internal/tcBufferReadback.h"
#include <d3d11.h>
#include <cstring>

namespace trussc {
namespace internal {
namespace {
class D3D11BufferReadback final : public BufferReadbackRequest {
public:
    D3D11BufferReadback(ID3D11DeviceContext* context, size_t size)
        : context_(context), bytes_(size) {}
    ~D3D11BufferReadback() override {
        // D3D11 retains resources used by outstanding commands after Release.
        if (query) query->Release();
        if (staging) staging->Release();
    }
    bool poll() override {
        BOOL complete = FALSE;
        const HRESULT status = context_->GetData(query, &complete, sizeof(complete),
                                                 D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (FAILED(status)) return true; // Includes device removal: result stays !ok().
        if (status == S_FALSE || !complete) return false;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
            std::memcpy(bytes_.data(), mapped.pData, bytes_.size());
            context_->Unmap(staging, 0);
            result = ReadbackResult(std::move(bytes_));
        }
        return true;
    }
    ID3D11Buffer* staging = nullptr;
    ID3D11Query* query = nullptr;
private:
    // The queue polls/destroys requests before the sokol context is shut down.
    ID3D11DeviceContext* context_;
    std::vector<unsigned char> bytes_;
};
} // namespace
} // namespace internal

std::unique_ptr<internal::BufferReadbackRequest> StorageBuffer::readAsyncPlatform() {
    auto* device = static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
    auto* context = static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
    auto* source = static_cast<ID3D11Buffer*>(const_cast<void*>(sg_d3d11_query_buffer_info(buffer_).buf));
    if (!device || !context || !source) return {};
    auto request = std::make_unique<internal::D3D11BufferReadback>(context, size_);
    D3D11_BUFFER_DESC desc = {};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;
    if (FAILED(device->CreateBuffer(&desc, nullptr, &request->staging))) return {};
    D3D11_QUERY_DESC queryDesc = {};
    queryDesc.Query = D3D11_QUERY_EVENT;
    if (FAILED(device->CreateQuery(&queryDesc, &request->query))) return {};
    // readAsync's suspendForImageUpload ended all passes and cleared SRV/UAVs,
    // as for synchronous read(). Snapshot now, before subsequent compute writes.
    context->CopyResource(request->staging, source);
    context->End(request->query);
    context->Flush(); // Submit once; GetData never flushes or waits in poll().
    return request;
}
} // namespace trussc
