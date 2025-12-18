// =============================================================================
// tcFbo_win.cpp - FBO のプラットフォーム固有実装（Windows / D3D11）
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

    // sokol から D3D11 デバイスとコンテキストを取得
    ID3D11Device* device = (ID3D11Device*)sg_d3d11_device();
    ID3D11DeviceContext* context = (ID3D11DeviceContext*)sg_d3d11_device_context();

    if (!device || !context) {
        tcLogError() << "[FBO] Failed to get D3D11 device/context";
        return false;
    }

    // colorTexture_ から sokol image info を取得
    sg_d3d11_image_info info = sg_d3d11_query_image_info(colorTexture_.getImage());
    ID3D11Texture2D* srcTexture = (ID3D11Texture2D*)info.tex2d;

    if (!srcTexture) {
        tcLogError() << "[FBO] Failed to get source D3D11 texture";
        return false;
    }

    // テクスチャの情報を取得
    D3D11_TEXTURE2D_DESC desc;
    srcTexture->GetDesc(&desc);

    // CPU 読み取り可能なステージングテクスチャを作成
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* stagingTexture = nullptr;
    HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr) || !stagingTexture) {
        tcLogError() << "[FBO] Failed to create staging texture";
        return false;
    }

    // ソーステクスチャをステージングテクスチャにコピー
    context->CopyResource(stagingTexture, srcTexture);

    // ステージングテクスチャをマップしてピクセルデータを読み取る
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        stagingTexture->Release();
        tcLogError() << "[FBO] Failed to map staging texture";
        return false;
    }

    // ピクセルをコピー（BGRA -> RGBA 変換）
    unsigned char* src = (unsigned char*)mapped.pData;
    unsigned char* dst = pixels;
    for (int y = 0; y < height_; y++) {
        unsigned char* srcRow = src + y * mapped.RowPitch;
        unsigned char* dstRow = dst + y * width_ * 4;
        for (int x = 0; x < width_; x++) {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2];  // R <- B
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1];  // G <- G
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0];  // B <- R
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3];  // A <- A
        }
    }

    context->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    return true;
}

} // namespace trussc

#endif // _WIN32
