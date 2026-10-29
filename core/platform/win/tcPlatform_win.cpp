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
#include "tcCaptureTrace_win.h"

// sokol_app のウィンドウ取得
#include "sokol_app_tc.h"

// stb_image_write（スクリーンショット保存用）
#include "stb/stb_image_write.h"

#pragma comment(lib, "shcore.lib")

namespace trussc {

void bringWindowToFront() {
    HWND hwnd = (HWND)sapp_win32_get_hwnd();
    if (hwnd) {
        SetForegroundWindow(hwnd);
    }
}

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

// Immersive mode (no-op on desktop)
void setImmersiveMode(bool enabled) { (void)enabled; }
bool getImmersiveMode() { return false; }

// Orientation (no-op on desktop)
void setOrientation(Orientation mask) { (void)mask; }

// SetThreadExecutionState belongs to the host main thread, including release.
namespace internal {
void applyKeepScreenOn(bool enabled, bool headless) {
    (void)headless;
    SetThreadExecutionState(enabled
        ? ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED | ES_CONTINUOUS
        : ES_CONTINUOUS);
}
} // namespace internal

// ---------------------------------------------------------------------------
// setWindowSize - ウィンドウサイズを変更
// ---------------------------------------------------------------------------
IVec2 getWindowPosition() {
    HWND hwnd = (HWND)sapp_win32_get_hwnd();
    if (!hwnd) return IVec2(-1, -1);

    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        return IVec2(rect.left, rect.top);
    }
    return IVec2(-1, -1);
}

void setWindowPosition(int x, int y) {
    HWND hwnd = (HWND)sapp_win32_get_hwnd();
    if (!hwnd) return;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

void setWindowDecorated(bool decorated) {
    HWND hwnd = (HWND)sapp_win32_get_hwnd();
    if (!hwnd) return;

    // Toggle the caption/border/system-menu bits. Keyboard focus and the quit
    // path (PostQuitMessage) are unaffected by this on Windows.
    const LONG_PTR chrome = WS_CAPTION | WS_THICKFRAME |
                            WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    if (decorated) style |= chrome;
    else           style &= ~chrome;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);

    // SWP_FRAMECHANGED makes the new (non-)client frame take effect.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void internal::setWindowSizeWin32(const void* handle, double logicalWidth, double logicalHeight) {
    HWND hwnd = (HWND)handle;
    if (!hwnd) {
        logWarning() << "[Platform] Failed to get window handle";
        return;
    }

    // Resolve the Windows 10 APIs dynamically so older systems still load.
    // Cache only the entry points: DPI must be read again after monitor moves.
    using GetWindowDpi = UINT (WINAPI*)(HWND);
    using AdjustRectForDpi = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    static const auto getWindowDpi = user32
        ? (GetWindowDpi)GetProcAddress(user32, "GetDpiForWindow") : nullptr;
    static const auto adjustRectForDpi = user32
        ? (AdjustRectForDpi)GetProcAddress(user32, "AdjustWindowRectExForDpi") : nullptr;
    // Match sokol's effective window scale when GetDpiForWindow is absent.
    UINT dpi = getWindowDpi ? getWindowDpi(hwnd) : 96;
    if (!dpi) dpi = 96;

    // Bound (abi): client and frame extents must fit SetWindowPos's positive int sizes. Overflow rejects the resize with one Warning and is counted.
    const auto rejectSize = [] {
        static std::uint64_t rejectedResizeCount = 0;
        if (rejectedResizeCount != (std::numeric_limits<std::uint64_t>::max)()) ++rejectedResizeCount;
        logWarning() << "[Platform] Window resize size is invalid or unrepresentable; rejected=" << rejectedResizeCount;
    };
    const int clientWidth = windowClientExtentPixels(logicalWidth, dpi);
    const int clientHeight = windowClientExtentPixels(logicalHeight, dpi);
    if (!clientWidth || !clientHeight) {
        rejectSize();
        return;
    }

    const DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    const DWORD exStyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    // Measure the frame on an empty client rect so native LONG arithmetic
    // cannot overflow while adding it to a large requested client extent.
    RECT rect = {0, 0, 0, 0};
    if (!adjustRectForDpi || !adjustRectForDpi(&rect, style, FALSE, exStyle, dpi)) {
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    }
    const int windowWidth = windowOuterExtentPixels(clientWidth, std::int64_t(rect.right) - rect.left);
    const int windowHeight = windowOuterExtentPixels(clientHeight, std::int64_t(rect.bottom) - rect.top);
    if (!windowWidth || !windowHeight) {
        rejectSize();
        return;
    }
    SetWindowPos(hwnd, nullptr, 0, 0, windowWidth, windowHeight,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void setWindowSizeLogical(int width, int height) {
    internal::setWindowSizeWin32(sapp_win32_get_hwnd(), width, height);
}

// ---------------------------------------------------------------------------
// getExecutablePath - 実行ファイルの絶対パスを取得
// ---------------------------------------------------------------------------
fs::path getExecutablePath() {
    // Native UTF-16 straight into fs::path — no UTF-8 round trip, so
    // non-ASCII install paths survive intact.
    // Bound (platform): the OS executable path uses MAX_PATH storage. Overflow returns the truncated path from GetModuleFileNameW.
    wchar_t path[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path);
}

// ---------------------------------------------------------------------------
// getExecutableDir - 実行ファイルがあるディレクトリを取得
// ---------------------------------------------------------------------------
fs::path getExecutableDir() {
    fs::path path = getExecutablePath();
    if (path.empty()) return {};
    return path.parent_path();
}

// ---------------------------------------------------------------------------
// User data / temp folders (getUserDataPath / getTempPath)
// ---------------------------------------------------------------------------
namespace {
// The executable name (without .exe) names the app's folder.
fs::path appFolderName() {
    fs::path name = getExecutablePath().stem();
    return name.empty() ? fs::path(L"TrussC") : name;
}

// An environment variable as a path (wide, so non-ASCII names survive)
fs::path envPath(const wchar_t* name) {
    DWORD cap = GetEnvironmentVariableW(name, nullptr, 0);
    while (cap > 0) {
        std::vector<wchar_t> buf(cap);
        const DWORD n = GetEnvironmentVariableW(name, buf.data(), cap);
        if (n == 0) return {};
        if (n < cap) return fs::path(buf.data());
        cap = n;   // the variable grew between the size query and the read
    }
    return {};
}
} // namespace

fs::path internal::platformUserDataRoot() {
    fs::path base = envPath(L"LOCALAPPDATA");
    if (base.empty()) {
        fs::path profile = envPath(L"USERPROFILE");
        if (profile.empty()) return platformTempRoot();
        base = profile / L"AppData" / L"Local";
    }
    return base / appFolderName();
}

fs::path internal::platformTempRoot() {
    DWORD cap = GetTempPathW(0, nullptr);   // %TMP%, else %TEMP%, ...
    while (cap > 0) {
        std::vector<wchar_t> buf(cap);
        const DWORD n = GetTempPathW(cap, buf.data());
        if (n == 0) break;
        if (n < cap) return fs::path(buf.data()) / appFolderName();
        cap = n + 1;
    }
    return fs::path(L"C:\\Windows\\Temp") / appFolderName();
}

fs::path internal::platformAppBundlePath() {
    return {};
}

// ---------------------------------------------------------------------------
// captureWindow - 現在のウィンドウをキャプチャ
// ---------------------------------------------------------------------------
namespace {
internal::D3D11CaptureTraceHook captureTraceHook = nullptr;
struct CaptureTraceScope {
    internal::D3D11CaptureTrace trace;
    internal::D3D11CaptureTraceHook hook;
    CaptureTraceScope() : hook(captureTraceHook) {
        const auto& ctx = internal::currentWindowContext();
        if (ctx.isMain) hook = nullptr;
        if (!hook) return;
        trace.isMain = ctx.isMain;
        trace.fbWidth = ctx.fbWidth; trace.fbHeight = ctx.fbHeight;
        trace.lastWidth = ctx.lastSwapchain.width; trace.lastHeight = ctx.lastSwapchain.height;
        trace.lastFormat = int(ctx.lastSwapchain.color_format); trace.lastSamples = ctx.lastSwapchain.sample_count;
        trace.lastRenderView = ctx.lastSwapchain.d3d11.render_view;
        trace.lastResolveView = ctx.lastSwapchain.d3d11.resolve_view;
    }
    ~CaptureTraceScope() { if (hook) hook(trace); }
};
}
void internal::setD3D11CaptureTraceForTests(D3D11CaptureTraceHook hook) {
    captureTraceHook = hook;
}

std::pair<size_t, size_t> internal::captureWindowSize(const internal::WindowContext& ctx) {
    // Acquisition-free estimate, also before B's first pass or after resize.
    // lastSwapchain may still refer to the previous native resource generation.
    if (!ctx.isMain) return {size_t(std::max(0, ctx.fbWidth)), size_t(std::max(0, ctx.fbHeight))};
    const void* view = sapp_get_swapchain().d3d11.render_view;
    if (view) {
        ID3D11Resource* resource = nullptr;
        ((ID3D11RenderTargetView*)view)->GetResource(&resource);
        if (resource) {
            ID3D11Texture2D* texture = nullptr;
            const HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture);
            resource->Release();
            if (SUCCEEDED(hr) && texture) {
                D3D11_TEXTURE2D_DESC desc;
                texture->GetDesc(&desc);
                texture->Release();
                return {size_t(desc.Width), size_t(desc.Height)};
            }
        }
    }
    return {size_t(std::max(0, sapp_width())), size_t(std::max(0, sapp_height()))};
}

bool captureWindow(Pixels& outPixels) {
    if (!sg_isvalid()) return false;
    if (internal::hasPublicFboActivation()) return false;
    internal::CapturePrefixScope capturePhase;
    CaptureTraceScope diagnostic;
    const auto source = internal::windowCaptureSource();
    const void* renderView = source.swapchain.d3d11.render_view;
    diagnostic.trace.selectedView = renderView;
    if (!renderView) {
        logError() << "[Screenshot] D3D11 render target view is null";
        return false;
    }

    // RenderTargetView からテクスチャを取得
    ID3D11RenderTargetView* rtv = (ID3D11RenderTargetView*)renderView;
    ID3D11Resource* resource = nullptr;
    rtv->GetResource(&resource);

    if (!resource) {
        logError() << "[Screenshot] Failed to get D3D11 resource";
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&backBuffer);
    resource->Release();

    if (FAILED(hr) || !backBuffer) {
        logError() << "[Screenshot] Failed to get back buffer texture";
        return false;
    }

    // テクスチャの情報を取得
    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);
    diagnostic.trace.selectedTexture = backBuffer;
    diagnostic.trace.width = desc.Width; diagnostic.trace.height = desc.Height;
    diagnostic.trace.format = unsigned(desc.Format); diagnostic.trace.samples = desc.SampleDesc.Count;
    try {
        mcp::internal::reconcileWindowCapture(desc.Width, desc.Height);
        diagnostic.trace.reconciledWidth = desc.Width; diagnostic.trace.reconciledHeight = desc.Height;
    } catch (...) {
        backBuffer->Release();
        throw;
    }

    const auto layout = internal::recorderPackedLayout(desc.Width, desc.Height,
                                                      internal::RecorderPixelFormat::RGBA8);
    // Bound (abi): D3D11 capture extent and format must fit the pixel layout. Overflow drops the recorder frame and is counted.
    if (!layout.byteSize || desc.Width > (UINT)std::numeric_limits<int>::max() ||
        desc.Height > (UINT)std::numeric_limits<int>::max() ||
        (desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM &&
         desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)) {
        backBuffer->Release();
        return false;
    }
    int width = (int)desc.Width;
    int height = (int)desc.Height;

    // sokol の D3D11 デバイスとコンテキストを取得
    ID3D11Device* device = (ID3D11Device*)sg_d3d11_device();
    ID3D11DeviceContext* context = (ID3D11DeviceContext*)sg_d3d11_device_context();

    if (!device || !context) {
        backBuffer->Release();
        logError() << "[Screenshot] Failed to get D3D11 device/context";
        return false;
    }

    // MSAA テクスチャかどうかをチェック
    bool isMSAA = (desc.SampleDesc.Count > 1);

    ID3D11Texture2D* sourceTexture = backBuffer;
    ID3D11Texture2D* resolvedTexture = nullptr;

    // MSAA の場合、まず非 MSAA テクスチャに解決する必要がある
    if (isMSAA) {
        D3D11_TEXTURE2D_DESC resolveDesc = desc;
        resolveDesc.SampleDesc.Count = 1;
        resolveDesc.SampleDesc.Quality = 0;
        resolveDesc.Usage = D3D11_USAGE_DEFAULT;
        resolveDesc.BindFlags = 0;
        resolveDesc.CPUAccessFlags = 0;
        resolveDesc.MiscFlags = 0;

        hr = device->CreateTexture2D(&resolveDesc, nullptr, &resolvedTexture);
        if (FAILED(hr) || !resolvedTexture) {
            backBuffer->Release();
            logError() << "[Screenshot] Failed to create resolve texture";
            return false;
        }

        // MSAA → 非 MSAA に解決
        context->ResolveSubresource(resolvedTexture, 0, backBuffer, 0, desc.Format);
        sourceTexture = resolvedTexture;
    }

    // CPU 読み取り可能なステージングテクスチャを作成
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.SampleDesc.Quality = 0;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* stagingTexture = nullptr;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr) || !stagingTexture) {
        if (resolvedTexture) resolvedTexture->Release();
        backBuffer->Release();
        logError() << "[Screenshot] Failed to create staging texture";
        return false;
    }

    // ソーステクスチャをステージングテクスチャにコピー
    context->CopyResource(stagingTexture, sourceTexture);

    // リソース解放
    if (resolvedTexture) resolvedTexture->Release();
    backBuffer->Release();


    // ステージングテクスチャをマップしてピクセルデータを読み取る
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        stagingTexture->Release();
        logError() << "[Screenshot] Failed to map staging texture";
        return false;
    }

    // Pixels allocation may fail after Map; always release that native lease.
    // Capture overwrites every byte; preserve a matching RGBA8 buffer.
    try {
        if (!outPixels.isAllocated() || outPixels.getWidth() != width ||
            outPixels.getHeight() != height || outPixels.getChannels() != 4 ||
            outPixels.getFormat() != PixelFormat::U8) {
            outPixels.allocate(width, height, 4);
        }
    }
    catch (const std::bad_alloc&) {
        context->Unmap(stagingTexture, 0); stagingTexture->Release();
        logError("Screenshot") << "pixel allocation failed";
        return false;
    }
    unsigned char* dst = outPixels.getData();
    size_t mappedBytes = 0;
    // Bound (abi): mapped D3D11 rows and destination capacity must cover the capture. Overflow drops the recorder frame and is counted.
    if (!dst || outPixels.getTotalBytes() < layout.byteSize || !mapped.pData ||
        mapped.RowPitch < layout.rowBytes ||
        !internal::recorderByteSize(mapped.RowPitch, height, mappedBytes)) {
        context->Unmap(stagingTexture, 0);
        stagingTexture->Release();
        return false;
    }

    bool isRGB10A2 = (desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM);

    unsigned char* src = (unsigned char*)mapped.pData;
    for (int y = 0; y < height; y++) {
        unsigned char* srcRow = src + (size_t)y * mapped.RowPitch;
        unsigned char* dstRow = dst + (size_t)y * layout.rowBytes;
        if (isRGB10A2) {
            // R10G10B10A2: [A:2 (31-30)][B:10 (29-20)][G:10 (19-10)][R:10 (9-0)]
            const uint32_t* srcPixels = (const uint32_t*)srcRow;
            for (size_t x = 0; x < (size_t)width; x++) {
                uint32_t pixel = srcPixels[x];
                uint32_t r10 = (pixel >>  0) & 0x3FF;
                uint32_t g10 = (pixel >> 10) & 0x3FF;
                uint32_t b10 = (pixel >> 20) & 0x3FF;
                uint32_t a2  = (pixel >> 30) & 0x3;
                dstRow[x * 4 + 0] = (uint8_t)(r10 >> 2);
                dstRow[x * 4 + 1] = (uint8_t)(g10 >> 2);
                dstRow[x * 4 + 2] = (uint8_t)(b10 >> 2);
                dstRow[x * 4 + 3] = (uint8_t)(a2 * 85);
            }
        } else {
            // BGRA8 fallback
            for (size_t x = 0; x < (size_t)width; x++) {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 2];  // R <- B
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1];  // G <- G
                dstRow[x * 4 + 2] = srcRow[x * 4 + 0];  // B <- R
                dstRow[x * 4 + 3] = srcRow[x * 4 + 3];  // A <- A
            }
        }
    }

    context->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    diagnostic.trace.succeeded = true;
    diagnostic.trace.pixelWidth = outPixels.getWidth(); diagnostic.trace.pixelHeight = outPixels.getHeight();
    return true;
}

// ---------------------------------------------------------------------------
// saveScreenshot - スクリーンショットをファイルに保存
// ---------------------------------------------------------------------------
bool internal::captureWindowToFile(const std::filesystem::path& requestedPath) {
    const auto path = internal::resolveScreenshotPath(requestedPath);
    Pixels pixels;
    if (!captureWindow(pixels)) return false;
    return internal::saveScreenshotPixels(pixels, path);
}

bool internal::saveScreenshotPixels(const Pixels& pixels, const std::filesystem::path& requestedPath) {
    return writeScreenshotPixels(pixels, requestedPath).ok();
}

SaveResult internal::writeScreenshotPixels(const Pixels& pixels, const std::filesystem::path& requestedPath) {
    const auto path = internal::resolveScreenshotPath(requestedPath);
    return internal::atomicWrite(path, [&](std::FILE* file) {
        return internal::writePixelsEncoded(file, pixels, toLower(getFileExtension(path)), path);
    }, "Screenshot", true);
}

// ---------------------------------------------------------------------------
// System sensors (stubs)
// ---------------------------------------------------------------------------
namespace internal {
void installAppMenu() {}  // macOS-only
} // namespace internal

float getSystemVolume() { return -1.0f; }
void setSystemVolume(float volume) { (void)volume; }
float getSystemBrightness() { return -1.0f; }
void setSystemBrightness(float brightness) { (void)brightness; }
ThermalState getThermalState() { return ThermalState::Nominal; }
float getThermalTemperature() { return -1.0f; }
float getBatteryLevel() { return -1.0f; }
bool isBatteryCharging() { return false; }
Vec3 getAccelerometer() { return Vec3(0, 0, 0); }
Vec3 getGyroscope() { return Vec3(0, 0, 0); }
Quaternion getDeviceOrientation() { return Quaternion(1, 0, 0, 0); }
float getCompassHeading() { return 0.0f; }
bool isProximityClose() { return false; }
Location getLocation() { return Location(); }

} // namespace trussc

#endif // _WIN32
