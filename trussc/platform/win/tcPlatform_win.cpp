// =============================================================================
// Windows プラットフォーム固有機能
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
#include <shellscalingapi.h>
#include <d3d11.h>

// sokol_app のウィンドウ取得
#include "sokol_app.h"

// stb_image_write（スクリーンショット保存用）
#include "stb/stb_image_write.h"

#pragma comment(lib, "shcore.lib")

namespace trussc {
namespace platform {

// ---------------------------------------------------------------------------
// getDisplayScaleFactor - DPIスケールを取得
// ---------------------------------------------------------------------------
float getDisplayScaleFactor() {
    // Windows 10 Anniversary Update (1607) 以降
    // GetDpiForSystem() を使用
    static bool initialized = false;
    static float scaleFactor = 1.0f;

    if (!initialized) {
        initialized = true;

        // SetProcessDpiAwarenessContext は sokol_app が行うはずなので
        // ここでは単にDPIを取得する

        // まずウィンドウハンドルを試す
        HWND hwnd = (HWND)sapp_win32_get_hwnd();
        if (hwnd) {
            // GetDpiForWindow は Windows 10 1607 以降で利用可能
            typedef UINT (WINAPI *GetDpiForWindowFunc)(HWND);
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32) {
                auto getDpiForWindow = (GetDpiForWindowFunc)GetProcAddress(user32, "GetDpiForWindow");
                if (getDpiForWindow) {
                    UINT dpi = getDpiForWindow(hwnd);
                    scaleFactor = dpi / 96.0f;
                    return scaleFactor;
                }
            }
        }

        // フォールバック: GetDpiForSystem
        typedef UINT (WINAPI *GetDpiForSystemFunc)();
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            auto getDpiForSystem = (GetDpiForSystemFunc)GetProcAddress(user32, "GetDpiForSystem");
            if (getDpiForSystem) {
                UINT dpi = getDpiForSystem();
                scaleFactor = dpi / 96.0f;
                return scaleFactor;
            }
        }

        // 最終フォールバック: GetDeviceCaps
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
            scaleFactor = dpiX / 96.0f;
            ReleaseDC(nullptr, hdc);
        }
    }

    return scaleFactor;
}

// ---------------------------------------------------------------------------
// setWindowSize - ウィンドウサイズを変更
// ---------------------------------------------------------------------------
void setWindowSize(int width, int height) {
    HWND hwnd = (HWND)sapp_win32_get_hwnd();
    if (!hwnd) {
        tcLogWarning() << "[Platform] Failed to get window handle";
        return;
    }

    // 現在のウィンドウスタイルを取得
    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    DWORD exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    // クライアント領域のサイズからウィンドウ全体のサイズを計算
    RECT rect = { 0, 0, width, height };
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;

    // 現在のウィンドウ位置を取得
    RECT currentRect;
    GetWindowRect(hwnd, &currentRect);

    // 位置を維持してサイズのみ変更
    SetWindowPos(hwnd, nullptr,
                 currentRect.left, currentRect.top,
                 windowWidth, windowHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------
// getExecutablePath - 実行ファイルの絶対パスを取得
// ---------------------------------------------------------------------------
std::string getExecutablePath() {
    wchar_t path[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    // UTF-16 から UTF-8 に変換
    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";

    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), size, nullptr, nullptr);

    return result;
}

// ---------------------------------------------------------------------------
// getExecutableDir - 実行ファイルがあるディレクトリを取得
// ---------------------------------------------------------------------------
std::string getExecutableDir() {
    std::string path = getExecutablePath();
    if (path.empty()) return "";

    // 最後の \ または / を探す
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        return path.substr(0, pos + 1);
    }

    return path;
}

// ---------------------------------------------------------------------------
// captureWindow - 現在のウィンドウをキャプチャ
// ---------------------------------------------------------------------------
bool captureWindow(Pixels& outPixels) {
    // sokol の swapchain から D3D11 テクスチャを取得
    sapp_swapchain sc = sapp_get_swapchain();
    if (!sc.d3d11.render_view) {
        tcLogError() << "[Screenshot] D3D11 render target view is null";
        return false;
    }

    // RenderTargetView からテクスチャを取得
    ID3D11RenderTargetView* rtv = (ID3D11RenderTargetView*)sc.d3d11.render_view;
    ID3D11Resource* resource = nullptr;
    rtv->GetResource(&resource);

    if (!resource) {
        tcLogError() << "[Screenshot] Failed to get D3D11 resource";
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&backBuffer);
    resource->Release();

    if (FAILED(hr) || !backBuffer) {
        tcLogError() << "[Screenshot] Failed to get back buffer texture";
        return false;
    }

    // テクスチャの情報を取得
    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);

    int width = (int)desc.Width;
    int height = (int)desc.Height;

    // sokol の D3D11 デバイスとコンテキストを取得
    ID3D11Device* device = (ID3D11Device*)sg_d3d11_device();
    ID3D11DeviceContext* context = (ID3D11DeviceContext*)sg_d3d11_device_context();

    if (!device || !context) {
        backBuffer->Release();
        tcLogError() << "[Screenshot] Failed to get D3D11 device/context";
        return false;
    }

    // CPU 読み取り可能なステージングテクスチャを作成
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* stagingTexture = nullptr;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr) || !stagingTexture) {
        backBuffer->Release();
        tcLogError() << "[Screenshot] Failed to create staging texture";
        return false;
    }

    // バックバッファをステージングテクスチャにコピー
    context->CopyResource(stagingTexture, backBuffer);
    backBuffer->Release();

    // ステージングテクスチャをマップしてピクセルデータを読み取る
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        stagingTexture->Release();
        tcLogError() << "[Screenshot] Failed to map staging texture";
        return false;
    }

    // Pixels を確保
    outPixels.allocate(width, height, 4);
    unsigned char* dst = outPixels.getData();

    // ピクセルをコピー（BGRA -> RGBA 変換）
    unsigned char* src = (unsigned char*)mapped.pData;
    for (int y = 0; y < height; y++) {
        unsigned char* srcRow = src + y * mapped.RowPitch;
        unsigned char* dstRow = dst + y * width * 4;
        for (int x = 0; x < width; x++) {
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

// ---------------------------------------------------------------------------
// saveScreenshot - スクリーンショットをファイルに保存
// ---------------------------------------------------------------------------
bool saveScreenshot(const std::filesystem::path& path) {
    // まず Pixels にキャプチャ
    Pixels pixels;
    if (!captureWindow(pixels)) {
        return false;
    }

    int width = pixels.getWidth();
    int height = pixels.getHeight();
    unsigned char* data = pixels.getData();

    // 拡張子から形式を判定
    std::string ext = path.extension().string();

    // 小文字に変換
    for (char& c : ext) {
        c = (char)std::tolower((unsigned char)c);
    }

    int result = 0;
    std::string pathStr = path.string();

    if (ext == ".png") {
        result = stbi_write_png(pathStr.c_str(), width, height, 4, data, width * 4);
    } else if (ext == ".jpg" || ext == ".jpeg") {
        result = stbi_write_jpg(pathStr.c_str(), width, height, 4, data, 90);
    } else if (ext == ".bmp") {
        result = stbi_write_bmp(pathStr.c_str(), width, height, 4, data);
    } else if (ext == ".tga") {
        result = stbi_write_tga(pathStr.c_str(), width, height, 4, data);
    } else {
        // デフォルトは PNG
        pathStr += ".png";
        result = stbi_write_png(pathStr.c_str(), width, height, 4, data, width * 4);
    }

    if (result) {
        tcLogVerbose() << "[Screenshot] Saved: " << pathStr;
        return true;
    } else {
        tcLogError() << "[Screenshot] Failed to save: " << pathStr;
        return false;
    }
}

} // namespace platform
} // namespace trussc

#endif // _WIN32
