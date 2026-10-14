// =============================================================================
// tcVideoGrabber_win.cpp - Windows Media Foundation 実装
// =============================================================================

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <dshow.h>

#include "TrussC.h"
#include "tcVideoGrabberPixels.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <cstring>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace trussc {

// ---------------------------------------------------------------------------
// プラットフォーム固有データ構造
// ---------------------------------------------------------------------------
struct VideoGrabberPlatformData {
    IMFMediaSource* mediaSource = nullptr;
    IMFSourceReader* sourceReader = nullptr;
    bool sourceShutDown = false;   // closePlatform() already shut it down

    ~VideoGrabberPlatformData() {
        if (sourceReader) sourceReader->Release();
        if (mediaSource) {
            if (!sourceShutDown) mediaSource->Shutdown();
            mediaSource->Release();
        }
        delete[] backBuffer;
    }

    // ダブルバッファリング用
    unsigned char* backBuffer = nullptr;
    std::atomic<bool> bufferReady{false};
    int bufferWidth = 0;
    int bufferHeight = 0;
    std::int32_t bufferStride = 0;

    // サイズ変更通知用
    std::atomic<bool> needsResize{false};
    int newWidth = 0;
    int newHeight = 0;

};

// ---------------------------------------------------------------------------
// Media Foundation 初期化ヘルパー
// ---------------------------------------------------------------------------
static bool g_mfInitialized = false;

static bool ensureMFInitialized() {
    if (g_mfInitialized) return true;

    HRESULT hr = MFStartup(MF_VERSION);
    if (SUCCEEDED(hr)) {
        g_mfInitialized = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// キャプチャスレッド関数
// ---------------------------------------------------------------------------
static void captureThreadFunc(std::shared_ptr<internal::GrabberSharedState> state) {
    auto* data = static_cast<VideoGrabberPlatformData*>(state->platformData.get());
    while (!state->stopped.load()) {
        IMFSample* sample = nullptr;
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;

        HRESULT hr = data->sourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &sample
        );

        // Check if we should exit (running flag was cleared or source was shut down)
        if (state->stopped.load()) {
            if (sample) sample->Release();
            break;
        }

        // Stamp arrival time on the capture thread, before any processing.
        // Stays accurate even when the app's main loop stalls.
        uint64_t arrivalUs = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        if (FAILED(hr) || !sample) {
            if (flags & MF_SOURCE_READERF_STREAMTICK) {
                continue;
            }
            // If not running, exit the loop
            if (state->stopped.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // サンプルからバッファを取得
        IMFMediaBuffer* buffer = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (SUCCEEDED(hr) && buffer) {
            BYTE* rawData = nullptr;
            DWORD maxLength = 0, currentLength = 0;
            hr = buffer->Lock(&rawData, &maxLength, &currentLength);

            if (SUCCEEDED(hr) && rawData) {
                int w = data->bufferWidth;
                int h = data->bufferHeight;
                if (internal::copyGrabberRGB32(data->backBuffer, rawData,
                        currentLength, w, h, data->bufferStride)) {

                    // Publish under the State lock, including pointer/size checks.
                    {
                        std::lock_guard<std::mutex> lock(state->mtx);
                        if (state->pixels && state->width == data->bufferWidth &&
                            state->height == data->bufferHeight) {
                            std::memcpy(state->pixels.get(), data->backBuffer,
                                        (size_t)data->bufferWidth * data->bufferHeight * 4);
                            state->dirty.store(true);
                        }
                    }
                    state->frameQueue.push(data->backBuffer, data->bufferWidth,
                                           data->bufferHeight, arrivalUs);
                }

                buffer->Unlock();
            }
            buffer->Release();
        }
        sample->Release();
    }
}

// ---------------------------------------------------------------------------
// listDevicesPlatform - デバイス一覧を取得
// ---------------------------------------------------------------------------
std::vector<VideoDeviceInfo> VideoGrabber::listDevicesPlatform() {
    std::vector<VideoDeviceInfo> devices;

    if (!ensureMFInitialized()) {
        logError() << "VideoGrabber: Failed to initialize Media Foundation";
        return devices;
    }

    IMFAttributes* attributes = nullptr;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) return devices;

    hr = attributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    if (FAILED(hr)) {
        attributes->Release();
        return devices;
    }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attributes, &ppDevices, &count);
    attributes->Release();

    if (FAILED(hr)) return devices;

    for (UINT32 i = 0; i < count; i++) {
        WCHAR* friendlyName = nullptr;
        UINT32 nameLength = 0;
        hr = ppDevices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
            &friendlyName,
            &nameLength
        );

        if (SUCCEEDED(hr) && friendlyName) {
            VideoDeviceInfo info;
            info.deviceId = (int)i;

            // WCHAR から UTF-8 に変換
            int size = WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, nullptr, 0, nullptr, nullptr);
            if (size > 0) {
                info.deviceName.resize(size - 1);
                WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, info.deviceName.data(), size, nullptr, nullptr);
            }
            info.uniqueId = std::to_string(i);

            devices.push_back(info);
            CoTaskMemFree(friendlyName);
        }

        ppDevices[i]->Release();
    }
    CoTaskMemFree(ppDevices);

    return devices;
}

// ---------------------------------------------------------------------------
// setupPlatform - カメラを開始
// ---------------------------------------------------------------------------
bool VideoGrabber::setupPlatform() {
    if (!ensureMFInitialized()) {
        logError() << "VideoGrabber: Failed to initialize Media Foundation";
        return false;
    }

    auto* data = new VideoGrabberPlatformData();
    state_->platformData = std::shared_ptr<VideoGrabberPlatformData>(data);

    // デバイスを列挙
    IMFAttributes* attributes = nullptr;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        state_->platformData.reset();
        return false;
    }

    hr = attributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attributes, &ppDevices, &count);
    attributes->Release();

    if (FAILED(hr) || count == 0) {
        logError() << "VideoGrabber: No video devices found";
        state_->platformData.reset();
        return false;
    }

    if (deviceId_ >= (int)count) {
        logError() << "VideoGrabber: Invalid device ID " << deviceId_;
        for (UINT32 i = 0; i < count; i++) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
        state_->platformData.reset();
        return false;
    }

    // デバイス名を取得
    WCHAR* friendlyName = nullptr;
    UINT32 nameLength = 0;
    hr = ppDevices[deviceId_]->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
        &friendlyName,
        &nameLength
    );
    if (SUCCEEDED(hr) && friendlyName) {
        int size = WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            deviceName_.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, deviceName_.data(), size, nullptr, nullptr);
        }
        CoTaskMemFree(friendlyName);
    }

    // メディアソースを作成
    hr = ppDevices[deviceId_]->ActivateObject(IID_PPV_ARGS(&data->mediaSource));
    for (UINT32 i = 0; i < count; i++) ppDevices[i]->Release();
    CoTaskMemFree(ppDevices);

    if (FAILED(hr)) {
        logError() << "VideoGrabber: Failed to activate media source";
        state_->platformData.reset();
        return false;
    }

    // ソースリーダーを作成
    IMFAttributes* readerAttributes = nullptr;
    MFCreateAttributes(&readerAttributes, 1);
    if (readerAttributes) {
        readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    hr = MFCreateSourceReaderFromMediaSource(data->mediaSource, readerAttributes, &data->sourceReader);
    if (readerAttributes) readerAttributes->Release();

    if (FAILED(hr)) {
        logError() << "VideoGrabber: Failed to create source reader";
        state_->platformData.reset();
        return false;
    }

    // Set output format to RGB32 (BGRA, better supported than RGB24)
    IMFMediaType* outputType = nullptr;
    MFCreateMediaType(&outputType);
    if (outputType) {
        outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, state_->requestedWidth, state_->requestedHeight);

        hr = data->sourceReader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
        if (FAILED(hr)) {
            logWarning() << "VideoGrabber: Failed to set RGB32 format, hr=" << std::hex << hr;
        }
        outputType->Release();
    }

    // Get actual format after setting
    IMFMediaType* currentType = nullptr;
    hr = data->sourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType);
    if (FAILED(hr) || !currentType) {
        logError() << "VideoGrabber: Cannot read current media type for requested "
                   << state_->requestedWidth << "x" << state_->requestedHeight;
        if (currentType) currentType->Release();
        closePlatform();
        return false;
    }

    GUID subtype = GUID_NULL;
    hr = currentType->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr) || subtype != MFVideoFormat_RGB32) {
        WCHAR subtypeName[40] = {};
        char subtypeText[40] = {};
        StringFromGUID2(subtype, subtypeName, 40);
        WideCharToMultiByte(CP_UTF8, 0, subtypeName, -1,
                            subtypeText, 40, nullptr, nullptr);
        logError() << "VideoGrabber: Requested " << state_->requestedWidth << "x"
                   << state_->requestedHeight << " requires RGB32; current subtype="
                   << (FAILED(hr) ? "unavailable" : subtypeText);
        currentType->Release();
        closePlatform();
        return false;
    }

    UINT32 w = 0, h = 0;
    MFGetAttributeSize(currentType, MF_MT_FRAME_SIZE, &w, &h);
    state_->width = (int)w;
    state_->height = (int)h;
    UINT32 stride = 0;
    if (SUCCEEDED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) {
        data->bufferStride = static_cast<std::int32_t>(stride);
    }
    if (data->bufferStride == 0) {
        LONG defaultStride = 0;
        // RGB32's Data1 is D3DFMT_X8R8G8B8; preserve the fallback's sign.
        if (FAILED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1,
                                                w, &defaultStride)) || defaultStride == 0) {
            defaultStride = state_->width * 4;
        }
        data->bufferStride = static_cast<std::int32_t>(defaultStride);
    }
    currentType->Release();

    // バックバッファを確保
    data->bufferWidth = state_->width;
    data->bufferHeight = state_->height;
    data->backBuffer = new unsigned char[state_->width * state_->height * 4];
    std::memset(data->backBuffer, 0, state_->width * state_->height * 4);

    // キャプチャスレッドを開始
    state_->keptThreads.start(state_->captureThread, [state = state_] {
        captureThreadFunc(state);
    });

    logNotice() << "VideoGrabber: Started capturing at " << state_->width << "x" << state_->height
                  << " from " << deviceName_;
    logNotice() << "VideoGrabber: RGB32 stride=" << data->bufferStride
                << (data->bufferStride < 0 ? " (bottom-up)" : " (top-down)");

    return true;
}

// ---------------------------------------------------------------------------
// closePlatform - カメラを停止
// ---------------------------------------------------------------------------
void VideoGrabber::closePlatform() {
    if (!state_->platformData) return;

    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());

    // Signal thread to stop
    state_->stopped.store(true);

    // Flush pending reads first
    if (data->sourceReader) {
        data->sourceReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    }

    // Shutdown media source - this will cause ReadSample to fail and return
    if (data->mediaSource) {
        data->mediaSource->Shutdown();
        data->sourceShutDown = true;
    }

    // close() joins next, before the State releases these resources.
}

// ---------------------------------------------------------------------------
// updatePlatform - フレーム更新（メインスレッドで呼ばれる）
// ---------------------------------------------------------------------------
void VideoGrabber::updatePlatform() {
    // キャプチャスレッドが非同期でフレームを取得するので
    // ここでは特に何もしない
}

// ---------------------------------------------------------------------------
// checkResizeNeeded - リサイズが必要かチェック
// ---------------------------------------------------------------------------
bool VideoGrabber::checkResizeNeeded() {
    if (!state_->platformData) return false;
    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    return data->needsResize.load();
}

// ---------------------------------------------------------------------------
// getNewSize - 新しいサイズを取得
// ---------------------------------------------------------------------------
void VideoGrabber::getNewSize(int& width, int& height) {
    if (!state_->platformData) {
        width = 0;
        height = 0;
        return;
    }
    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    width = data->newWidth;
    height = data->newHeight;
}

// ---------------------------------------------------------------------------
// clearResizeFlag - リサイズフラグをクリア
// ---------------------------------------------------------------------------
void VideoGrabber::clearResizeFlag() {
    if (!state_->platformData) return;
    auto* data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    data->needsResize.store(false);
}

// ---------------------------------------------------------------------------
// updateDelegatePixels - ピクセルバッファ確保後にターゲットを更新
// ---------------------------------------------------------------------------
void VideoGrabber::updateDelegatePixels() {
    // Callbacks read pixels directly from the State under its lock.
}

// ---------------------------------------------------------------------------
// checkCameraPermission - カメラ権限を確認
// ---------------------------------------------------------------------------
bool VideoGrabber::checkCameraPermission() {
    // Windows ではカメラ権限はシステム設定で管理される
    // アプリからの確認 API は限定的なので、常に true を返す
    return true;
}

// ---------------------------------------------------------------------------
// requestCameraPermission - カメラ権限をリクエスト
// ---------------------------------------------------------------------------
void VideoGrabber::requestCameraPermission() {
    // Windows ではシステム設定からカメラ権限を有効にする必要がある
    // アプリから直接リクエストする標準 API はない
    logNotice() << "VideoGrabber: Please enable camera access in Windows Settings > Privacy > Camera";
}

} // namespace trussc

#endif // _WIN32
