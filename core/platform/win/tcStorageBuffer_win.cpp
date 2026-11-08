#include "TrussC.h"
#if defined(_WIN32)
#include <d3d11.h>

namespace trussc {
bool StorageBuffer::readPlatform(void* destination, size_t bytes) {
    auto* device = static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
    auto* context = static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
    auto* source = static_cast<ID3D11Buffer*>(const_cast<void*>(sg_d3d11_query_buffer_info(buffer_).buf));
    if (!device || !context || !source) return false;
    D3D11_BUFFER_DESC desc = {};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;
    ID3D11Buffer* staging = nullptr;
    if (FAILED(device->CreateBuffer(&desc, nullptr, &staging))) return false;
    // suspendForImageUpload ends all passes; sokol's D3D11 end_pass clears
    // SRV/UAV bindings. CopyResource and Map do not change pipeline bindings.
    context->CopyResource(staging, source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const bool ok = SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
    if (ok) {
        std::memcpy(destination, mapped.pData, bytes);
        context->Unmap(staging, 0);
    }
    staging->Release();
    return ok;
}
} // namespace trussc
#endif
