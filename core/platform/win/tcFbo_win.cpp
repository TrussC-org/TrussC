// =============================================================================
// tcFbo_win.cpp - FBO pixel readback (Windows / D3D11)
// =============================================================================

#include "TrussC.h"

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

bool Fbo::readPixelsPlatform(unsigned char* pixels) const {
    if (!allocated_ || !pixels) return false;

    ID3D11Device* device = (ID3D11Device*)sg_d3d11_device();
    ID3D11DeviceContext* context = (ID3D11DeviceContext*)sg_d3d11_device_context();

    if (!device || !context) {
        logError() << "[FBO] Failed to get D3D11 device/context";
        return false;
    }

    sg_d3d11_image_info info = sg_d3d11_query_image_info(colorTexture_.getImage());
    ID3D11Texture2D* srcTexture = (ID3D11Texture2D*)info.tex2d;

    if (!srcTexture) {
        logError() << "[FBO] Failed to get source D3D11 texture";
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    srcTexture->GetDesc(&desc);

    // CPU-readable staging texture
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

    // Copy pixels row-by-row (handle RowPitch != width*4 stride)
    // sokol maps SG_PIXELFORMAT_RGBA8 to DXGI_FORMAT_R8G8B8A8_UNORM,
    // so the data is already in RGBA order — no channel swizzle needed.
    unsigned char* src = (unsigned char*)mapped.pData;
    unsigned char* dst = pixels;
    size_t rowBytes = (size_t)width_ * 4;
    for (int y = 0; y < height_; y++) {
        memcpy(dst + y * rowBytes, src + y * mapped.RowPitch, rowBytes);
    }

    context->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    return true;
}

bool Fbo::readPixelsFloatPlatform(float* pixels) const {
    if (!allocated_ || !pixels) return false;

    ID3D11Device* device = (ID3D11Device*)sg_d3d11_device();
    ID3D11DeviceContext* context = (ID3D11DeviceContext*)sg_d3d11_device_context();

    if (!device || !context) {
        logError() << "[FBO] Failed to get D3D11 device/context";
        return false;
    }

    sg_d3d11_image_info info = sg_d3d11_query_image_info(colorTexture_.getImage());
    ID3D11Texture2D* srcTexture = (ID3D11Texture2D*)info.tex2d;

    if (!srcTexture) {
        logError() << "[FBO] Failed to get source D3D11 texture";
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    srcTexture->GetDesc(&desc);

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

    int ch = channelCount(format_);
    int bpp = bytesPerPixel(format_);
    sg_pixel_format sgFmt = colorTexture_.getPixelFormat();
    if (sgFmt == SG_PIXELFORMAT_NONE) sgFmt = SG_PIXELFORMAT_RGBA8;

    // 32F: direct memcpy per row
    if (sgFmt == SG_PIXELFORMAT_R32F || sgFmt == SG_PIXELFORMAT_RG32F || sgFmt == SG_PIXELFORMAT_RGBA32F) {
        size_t rowBytes = width_ * bpp;
        for (int y = 0; y < height_; y++) {
            memcpy((char*)pixels + y * rowBytes, (char*)mapped.pData + y * mapped.RowPitch, rowBytes);
        }
    }
    // 16F: read half-floats, convert to float
    else if (sgFmt == SG_PIXELFORMAT_R16F || sgFmt == SG_PIXELFORMAT_RG16F || sgFmt == SG_PIXELFORMAT_RGBA16F) {
        for (int y = 0; y < height_; y++) {
            uint16_t* srcRow = (uint16_t*)((char*)mapped.pData + y * mapped.RowPitch);
            float* dstRow = pixels + y * width_ * ch;
            for (int i = 0; i < width_ * ch; i++) {
                uint16_t h = srcRow[i];
                uint32_t sign = (h & 0x8000) << 16;
                uint32_t exp = (h >> 10) & 0x1F;
                uint32_t mantissa = h & 0x3FF;
                uint32_t f;
                if (exp == 0) {
                    f = (mantissa == 0) ? sign : (sign | ((127 - 14) << 23) | (mantissa << 13));
                } else if (exp == 31) {
                    f = sign | 0x7F800000 | (mantissa << 13);
                } else {
                    f = sign | ((exp + 127 - 15) << 23) | (mantissa << 13);
                }
                memcpy(&dstRow[i], &f, sizeof(float));
            }
        }
    }
    // U8: convert to float
    else {
        for (int y = 0; y < height_; y++) {
            unsigned char* srcRow = (unsigned char*)mapped.pData + y * mapped.RowPitch;
            float* dstRow = pixels + y * width_ * ch;
            for (int i = 0; i < width_ * ch; i++) {
                dstRow[i] = (float)srcRow[i] / 255.0f;
            }
        }
    }

    context->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    return true;
}

} // namespace trussc

#endif // _WIN32
