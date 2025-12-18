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

#include <atomic>
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

    // キャプチャスレッド
    std::thread captureThread;
    std::atomic<bool> running{false};

    // ダブルバッファリング用
    unsigned char* backBuffer = nullptr;
    std::atomic<bool> bufferReady{false};
    int bufferWidth = 0;
    int bufferHeight = 0;

    // サイズ変更通知用
    std::atomic<bool> needsResize{false};
    int newWidth = 0;
    int newHeight = 0;

    // スレッド同期
    std::mutex mutex;

    // ターゲットバッファ（メインスレッドのピクセルバッファを指す）
    unsigned char* targetPixels = nullptr;
    std::atomic<bool>* pixelsDirty = nullptr;
    std::mutex* mainMutex = nullptr;
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
static void captureThreadFunc(VideoGrabberPlatformData* data) {
    while (data->running) {
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

        if (FAILED(hr) || !sample) {
            if (flags & MF_SOURCE_READERF_STREAMTICK) {
                continue;
            }
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

                // バックバッファにコピー（RGB24 -> RGBA 変換）
                if (data->backBuffer && w > 0 && h > 0) {
                    // サンプルの形式によって変換が異なる
                    // ここでは RGB24 を想定（Media Foundation のデフォルト）
                    unsigned char* src = rawData;
                    unsigned char* dst = data->backBuffer;

                    // Media Foundation の RGB24 は上下反転していることが多い
                    for (int y = 0; y < h; y++) {
                        int srcY = h - 1 - y;  // 上下反転
                        unsigned char* srcRow = src + srcY * w * 3;
                        unsigned char* dstRow = dst + y * w * 4;
                        for (int x = 0; x < w; x++) {
                            dstRow[x * 4 + 0] = srcRow[x * 3 + 2];  // R <- B (BGR->RGB)
                            dstRow[x * 4 + 1] = srcRow[x * 3 + 1];  // G
                            dstRow[x * 4 + 2] = srcRow[x * 3 + 0];  // B <- R
                            dstRow[x * 4 + 3] = 255;                 // A
                        }
                    }

                    // メインスレッドのバッファにコピー
                    if (data->targetPixels && data->mainMutex && !data->needsResize.load()) {
                        std::lock_guard<std::mutex> lock(*data->mainMutex);
                        std::memcpy(data->targetPixels, data->backBuffer, w * h * 4);
                    }

                    if (data->pixelsDirty) {
                        data->pixelsDirty->store(true);
                    }
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
        tcLogError() << "VideoGrabber: Failed to initialize Media Foundation";
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
        tcLogError() << "VideoGrabber: Failed to initialize Media Foundation";
        return false;
    }

    auto* data = new VideoGrabberPlatformData();
    platformHandle_ = data;

    // デバイスを列挙
    IMFAttributes* attributes = nullptr;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        delete data;
        platformHandle_ = nullptr;
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
        tcLogError() << "VideoGrabber: No video devices found";
        delete data;
        platformHandle_ = nullptr;
        return false;
    }

    if (deviceId_ >= (int)count) {
        tcLogError() << "VideoGrabber: Invalid device ID " << deviceId_;
        for (UINT32 i = 0; i < count; i++) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
        delete data;
        platformHandle_ = nullptr;
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
        tcLogError() << "VideoGrabber: Failed to activate media source";
        delete data;
        platformHandle_ = nullptr;
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
        tcLogError() << "VideoGrabber: Failed to create source reader";
        data->mediaSource->Release();
        delete data;
        platformHandle_ = nullptr;
        return false;
    }

    // 出力フォーマットを RGB24 に設定
    IMFMediaType* outputType = nullptr;
    MFCreateMediaType(&outputType);
    if (outputType) {
        outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
        outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, requestedWidth_, requestedHeight_);

        hr = data->sourceReader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType);
        outputType->Release();
    }

    // 実際のフォーマットを取得
    IMFMediaType* currentType = nullptr;
    hr = data->sourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType);
    if (SUCCEEDED(hr) && currentType) {
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(currentType, MF_MT_FRAME_SIZE, &w, &h);
        width_ = (int)w;
        height_ = (int)h;
        currentType->Release();
    } else {
        width_ = requestedWidth_;
        height_ = requestedHeight_;
    }

    // バックバッファを確保
    data->bufferWidth = width_;
    data->bufferHeight = height_;
    data->backBuffer = new unsigned char[width_ * height_ * 4];
    std::memset(data->backBuffer, 0, width_ * height_ * 4);

    // ターゲット設定（updateDelegatePixels で設定される）
    data->mainMutex = &mutex_;
    data->pixelsDirty = &pixelsDirty_;

    // キャプチャスレッドを開始
    data->running = true;
    data->captureThread = std::thread(captureThreadFunc, data);

    tcLogNotice() << "VideoGrabber: Started capturing at " << width_ << "x" << height_
                  << " from " << deviceName_;

    return true;
}

// ---------------------------------------------------------------------------
// closePlatform - カメラを停止
// ---------------------------------------------------------------------------
void VideoGrabber::closePlatform() {
    if (!platformHandle_) return;

    auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);

    // キャプチャスレッドを停止
    data->running = false;
    if (data->captureThread.joinable()) {
        data->captureThread.join();
    }

    // リソースを解放
    if (data->sourceReader) {
        data->sourceReader->Release();
        data->sourceReader = nullptr;
    }

    if (data->mediaSource) {
        data->mediaSource->Shutdown();
        data->mediaSource->Release();
        data->mediaSource = nullptr;
    }

    if (data->backBuffer) {
        delete[] data->backBuffer;
        data->backBuffer = nullptr;
    }

    delete data;
    platformHandle_ = nullptr;
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
    if (!platformHandle_) return false;
    auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    return data->needsResize.load();
}

// ---------------------------------------------------------------------------
// getNewSize - 新しいサイズを取得
// ---------------------------------------------------------------------------
void VideoGrabber::getNewSize(int& width, int& height) {
    if (!platformHandle_) {
        width = 0;
        height = 0;
        return;
    }
    auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    width = data->newWidth;
    height = data->newHeight;
}

// ---------------------------------------------------------------------------
// clearResizeFlag - リサイズフラグをクリア
// ---------------------------------------------------------------------------
void VideoGrabber::clearResizeFlag() {
    if (!platformHandle_) return;
    auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    data->needsResize.store(false);
}

// ---------------------------------------------------------------------------
// updateDelegatePixels - ピクセルバッファ確保後にターゲットを更新
// ---------------------------------------------------------------------------
void VideoGrabber::updateDelegatePixels() {
    if (!platformHandle_) return;

    auto* data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    data->targetPixels = pixels_;
    if (verbose_) {
        tcLogVerbose() << "VideoGrabber: Updated target pixels pointer to " << (void*)pixels_;
    }
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
    tcLogNotice() << "VideoGrabber: Please enable camera access in Windows Settings > Privacy > Camera";
}

} // namespace trussc

#endif // _WIN32
