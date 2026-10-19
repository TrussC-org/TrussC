// =============================================================================
// tcFbo_win.cpp - FBO pixel readback (Windows / D3D11)
// =============================================================================

#include "TrussC.h"
#include "../tcFboConversion.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>

namespace trussc {

static DXGI_FORMAT readbackDXGIFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::R8: return DXGI_FORMAT_R8_UNORM;
        case TextureFormat::RG8: return DXGI_FORMAT_R8G8_UNORM;
        case TextureFormat::RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::BGRA8: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::RGBA16: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case TextureFormat::R16F: return DXGI_FORMAT_R16_FLOAT;
        case TextureFormat::RG16F: return DXGI_FORMAT_R16G16_FLOAT;
        case TextureFormat::RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case TextureFormat::R32F: return DXGI_FORMAT_R32_FLOAT;
        case TextureFormat::RG32F: return DXGI_FORMAT_R32G32_FLOAT;
        case TextureFormat::RGBA32F: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

bool Fbo::readPixelsPlatform(unsigned char* pixels, bool reportError) const {
    if (!allocated_ || !pixels) return false;

    size_t nativeRowBytes, nativeBytes, outputRowBytes, outputBytes;
    // Bound (abi): native and output rows must fit size_t before staging or copying. Overflow drops the recorder frame and is counted.
    if (!internal::fboReadbackLayout(target_.width, target_.height, bytesPerPixel(format_), nativeRowBytes, nativeBytes) ||
        !internal::fboReadbackLayout(target_.width, target_.height, 4, outputRowBytes, outputBytes)) return false;

    ID3D11Device* device = (ID3D11Device*)sg_d3d11_device();
    ID3D11DeviceContext* context = (ID3D11DeviceContext*)sg_d3d11_device_context();

    if (!device || !context) {
        if (reportError) logError() << "[FBO] Failed to get D3D11 device/context";
        return false;
    }

    sg_d3d11_image_info info = sg_d3d11_query_image_info(curColorTex_().getImage());
    ID3D11Texture2D* srcTexture = (ID3D11Texture2D*)info.tex2d;

    if (!srcTexture) {
        if (reportError) logError() << "[FBO] Failed to get source D3D11 texture";
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    srcTexture->GetDesc(&desc);

    // Bound (abi): D3D11 source extent and format must match the Fbo readback. Overflow drops the recorder frame and is counted.
    if (desc.Width != (UINT)target_.width || desc.Height != (UINT)target_.height ||
        desc.Format != readbackDXGIFormat(format_)) return false;

    // CPU-readable staging texture
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* stagingTexture = nullptr;
    HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr) || !stagingTexture) {
        if (reportError) logError() << "[FBO] Failed to create staging texture";
        return false;
    }

    context->CopyResource(stagingTexture, srcTexture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        stagingTexture->Release();
        if (reportError) logError() << "[FBO] Failed to map staging texture";
        return false;
    }

    size_t mappedBytes = 0;
    // Bound (abi): mapped D3D11 rows must hold the complete Fbo readback. Overflow drops the recorder frame and is counted.
    if (!mapped.pData || mapped.RowPitch < nativeRowBytes ||
        !internal::recorderByteSize(mapped.RowPitch, size_t(target_.height), mappedBytes)) {
        context->Unmap(stagingTexture, 0);
        stagingTexture->Release();
        return false;
    }

    // Mapped rows may include driver padding. Convert their actual format,
    // including half/float, R/RG expansion, BGRA swizzle and 16-bit UNORM.
    const auto* src = static_cast<const unsigned char*>(mapped.pData);
    for (int y = 0; y < target_.height; ++y) {
        internal::fboRowToRgba8(src + size_t(y) * mapped.RowPitch,
                              pixels + size_t(y) * outputRowBytes, target_.width, format_);
    }

    context->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    return true;
}

bool Fbo::readPixelsFloatPlatform(float* pixels) const {
    if (!allocated_ || !pixels) return false;

    size_t nativeRowBytes, nativeBytes, outputRowBytes, outputBytes;
    // Bound (abi): native and output rows must fit size_t before staging or copying. Overflow drops the recorder frame and is counted.
    if (!internal::fboReadbackLayout(target_.width, target_.height, bytesPerPixel(format_), nativeRowBytes, nativeBytes) ||
        !internal::fboReadbackLayout(target_.width, target_.height, size_t(channelCount(format_)) * sizeof(float), outputRowBytes, outputBytes)) return false;

    ID3D11Device* device = (ID3D11Device*)sg_d3d11_device();
    ID3D11DeviceContext* context = (ID3D11DeviceContext*)sg_d3d11_device_context();

    if (!device || !context) {
        logError() << "[FBO] Failed to get D3D11 device/context";
        return false;
    }

    sg_d3d11_image_info info = sg_d3d11_query_image_info(curColorTex_().getImage());
    ID3D11Texture2D* srcTexture = (ID3D11Texture2D*)info.tex2d;

    if (!srcTexture) {
        logError() << "[FBO] Failed to get source D3D11 texture";
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    srcTexture->GetDesc(&desc);

    // Bound (abi): D3D11 source extent and format must match the Fbo readback. Overflow drops the recorder frame and is counted.
    if (desc.Width != (UINT)target_.width || desc.Height != (UINT)target_.height ||
        desc.Format != readbackDXGIFormat(format_)) return false;

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* stagingTexture = nullptr;
    HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr) || !stagingTexture) {
        logError() << "[FBO] Failed to create staging texture";
        return false;
    }

    context->CopyResource(stagingTexture, srcTexture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        stagingTexture->Release();
        logError() << "[FBO] Failed to map staging texture";
        return false;
    }

    size_t mappedBytes = 0;
    // Bound (abi): mapped D3D11 rows must hold the complete Fbo readback. Overflow drops the recorder frame and is counted.
    if (!mapped.pData || mapped.RowPitch < nativeRowBytes ||
        !internal::recorderByteSize(mapped.RowPitch, size_t(target_.height), mappedBytes)) {
        context->Unmap(stagingTexture, 0);
        stagingTexture->Release();
        return false;
    }

    int ch = channelCount(format_);
    sg_pixel_format sgFmt = curColorTex_().getPixelFormat();
    if (sgFmt == SG_PIXELFORMAT_NONE) sgFmt = SG_PIXELFORMAT_RGBA8;

    // 32F: direct memcpy per row
    if (sgFmt == SG_PIXELFORMAT_R32F || sgFmt == SG_PIXELFORMAT_RG32F || sgFmt == SG_PIXELFORMAT_RGBA32F) {
        size_t rowBytes = nativeRowBytes;
        for (int y = 0; y < target_.height; y++) {
            memcpy((char*)pixels + y * rowBytes, (char*)mapped.pData + size_t(y) * mapped.RowPitch, rowBytes);
        }
    }
    // 16F: read half-floats, convert to float
    else if (sgFmt == SG_PIXELFORMAT_R16F || sgFmt == SG_PIXELFORMAT_RG16F || sgFmt == SG_PIXELFORMAT_RGBA16F) {
        for (int y = 0; y < target_.height; y++) {
            uint16_t* srcRow = (uint16_t*)((char*)mapped.pData + size_t(y) * mapped.RowPitch);
            float* dstRow = pixels + size_t(y) * (outputRowBytes / sizeof(float));
            for (size_t i = 0; i < outputRowBytes / sizeof(float); i++) {
                dstRow[i] = internal::fboHalfToFloat(srcRow[i]);
            }
        }
    }
    else if (sgFmt == SG_PIXELFORMAT_RGBA16) {
        for (int y = 0; y < target_.height; ++y) {
            const auto* srcRow = reinterpret_cast<const uint16_t*>(static_cast<const char*>(mapped.pData) + size_t(y) * mapped.RowPitch);
            for (size_t i = 0; i < outputRowBytes / sizeof(float); ++i)
                pixels[size_t(y) * target_.width * 4 + i] = float(srcRow[i]) / 65535.0f;
        }
    }
    // U8: convert to float
    else {
        for (int y = 0; y < target_.height; y++) {
            unsigned char* srcRow = (unsigned char*)mapped.pData + size_t(y) * mapped.RowPitch;
            float* dstRow = pixels + size_t(y) * (outputRowBytes / sizeof(float));
            for (size_t i = 0; i < outputRowBytes / sizeof(float); i++) {
                const size_t source = sgFmt == SG_PIXELFORMAT_BGRA8 && i % 4 < 3 ? (i / 4) * 4 + 2 - i % 4 : i;
                dstRow[i] = (float)srcRow[source] / 255.0f;
            }
        }
    }

    context->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    return true;
}

} // namespace trussc

#endif // _WIN32
