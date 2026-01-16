// =============================================================================
// tcVideoPlayer_win.cpp - Windows VideoPlayer implementation using Media Foundation
// =============================================================================
// Uses IMFMediaEngine for hardware-accelerated video decoding.
// D3D11 textures are injected directly into sokol_gfx.
//
// Reference: openFrameworks ofMediaFoundationPlayer (MIT License)
// Based on code by Andrew Wright (https://github.com/axjxwright/AX-MediaPlayer/)
// =============================================================================

#ifdef _WIN32

#include "TrussC.h"

#include <windows.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <d3d11.h>
#include <wrl/client.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "d3d11.lib")

using namespace Microsoft::WRL;
using namespace trussc;

// =============================================================================
// Forward declaration
// =============================================================================
class TCVideoPlayerImpl;

// =============================================================================
// Media Foundation initialization counter (thread-safe)
// =============================================================================
static std::atomic<int> sMFRefCount{0};

static bool InitMediaFoundation() {
    if (sMFRefCount.fetch_add(1) == 0) {
        HRESULT hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) {
            logError("VideoPlayer") << "Failed to initialize Media Foundation";
            sMFRefCount--;
            return false;
        }
    }
    return true;
}

static void CloseMediaFoundation() {
    if (sMFRefCount.fetch_sub(1) == 1) {
        MFShutdown();
    }
}

// =============================================================================
// IMFMediaEngineNotify implementation for event callbacks
// =============================================================================
class MediaEngineNotify : public IMFMediaEngineNotify {
public:
    MediaEngineNotify(TCVideoPlayerImpl* owner) : owner_(owner), refCount_(1) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IMFMediaEngineNotify)) {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&refCount_);
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = InterlockedDecrement(&refCount_);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IMFMediaEngineNotify
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) override;

private:
    TCVideoPlayerImpl* owner_;
    LONG refCount_;
};

// =============================================================================
// TCVideoPlayerImpl - Windows implementation
// =============================================================================
class TCVideoPlayerImpl {
public:
    TCVideoPlayerImpl() = default;
    ~TCVideoPlayerImpl() { close(); }

    bool load(const std::string& path, VideoPlayer* player);
    void close();
    void play();
    void stop();
    void setPaused(bool paused);
    void update(VideoPlayer* player);

    bool hasNewFrame() const { return hasNewFrame_; }
    bool isFinished() const { return isFinished_; }

    float getPosition() const;
    void setPosition(float pct);
    float getDuration() const;

    void setVolume(float vol);
    void setSpeed(float speed);
    void setLoop(bool loop);

    int getCurrentFrame() const;
    int getTotalFrames() const;
    void setFrame(int frame);
    void nextFrame();
    void previousFrame();

    // Event handler (called from MediaEngineNotify)
    void onMediaEvent(DWORD event, DWORD_PTR param1, DWORD param2);

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // Audio track info
    bool hasAudio() const { return hasAudio_; }
    uint32_t getAudioCodec() const { return audioCodec_; }
    int getAudioSampleRate() const { return audioSampleRate_; }
    int getAudioChannels() const { return audioChannels_; }
    std::vector<uint8_t> getAudioData() const;

private:
    bool createD3D11Device();
    bool createMediaEngine(const std::string& path);
    bool createRenderTexture();
    bool transferVideoFrame();
    bool loadAudioInfo(const std::string& path);

    // D3D11 resources
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<IMFDXGIDeviceManager> dxgiManager_;
    UINT dxgiResetToken_ = 0;

    // Media Engine
    ComPtr<IMFMediaEngine> mediaEngine_;
    ComPtr<IMFMediaEngineEx> mediaEngineEx_;
    MediaEngineNotify* eventNotify_ = nullptr;

    // Render target texture (D3D11)
    ComPtr<ID3D11Texture2D> renderTexture_;
    ComPtr<ID3D11Texture2D> stagingTexture_;

    // State
    int width_ = 0;
    int height_ = 0;
    float duration_ = 0.0f;
    float frameRate_ = 30.0f;
    bool isLoaded_ = false;
    bool isReady_ = false;
    bool hasNewFrame_ = false;
    bool isFinished_ = false;
    bool isLoop_ = false;

    // Pixel buffer for CPU readback
    unsigned char* pixels_ = nullptr;

    // Audio track info
    bool hasAudio_ = false;
    uint32_t audioCodec_ = 0;
    int audioSampleRate_ = 0;
    int audioChannels_ = 0;
    std::string mediaPath_;

    CRITICAL_SECTION criticalSection_;
    bool csInitialized_ = false;
};

// =============================================================================
// MediaEngineNotify::EventNotify implementation
// =============================================================================
STDMETHODIMP MediaEngineNotify::EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) {
    if (owner_) {
        owner_->onMediaEvent(event, param1, param2);
    }
    return S_OK;
}

// =============================================================================
// TCVideoPlayerImpl implementation
// =============================================================================

bool TCVideoPlayerImpl::createD3D11Device() {
    // Create D3D11 device with video support
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL featureLevel;
    UINT flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &d3dDevice_,
        &featureLevel,
        &d3dContext_
    );

    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create D3D11 device";
        return false;
    }

    // Enable multi-threading
    ComPtr<ID3D10Multithread> multithread;
    if (SUCCEEDED(d3dDevice_.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }

    // Create DXGI device manager
    hr = MFCreateDXGIDeviceManager(&dxgiResetToken_, &dxgiManager_);
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create DXGI device manager";
        return false;
    }

    hr = dxgiManager_->ResetDevice(d3dDevice_.Get(), dxgiResetToken_);
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to reset DXGI device";
        return false;
    }

    return true;
}

bool TCVideoPlayerImpl::createMediaEngine(const std::string& path) {
    HRESULT hr;

    // Create class factory
    ComPtr<IMFMediaEngineClassFactory> factory;
    hr = CoCreateInstance(
        CLSID_MFMediaEngineClassFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );

    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create MediaEngine factory";
        return false;
    }

    // Create attributes
    ComPtr<IMFAttributes> attributes;
    hr = MFCreateAttributes(&attributes, 3);
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create attributes";
        return false;
    }

    // Set DXGI manager for HW acceleration
    hr = attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, dxgiManager_.Get());
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to set DXGI manager";
        return false;
    }

    // Set callback
    eventNotify_ = new MediaEngineNotify(this);
    hr = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, eventNotify_);
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to set callback";
        return false;
    }

    // Set output format (BGRA for D3D11)
    hr = attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to set output format";
        return false;
    }

    // Create media engine
    hr = factory->CreateInstance(
        MF_MEDIA_ENGINE_REAL_TIME_MODE,
        attributes.Get(),
        &mediaEngine_
    );

    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create MediaEngine";
        return false;
    }

    // Get extended interface
    mediaEngine_.As(&mediaEngineEx_);

    // Disable autoplay
    mediaEngine_->SetAutoPlay(FALSE);

    // Convert path to wide string and BSTR
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring widePath(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &widePath[0], wideLen);

    BSTR bstrPath = SysAllocString(widePath.c_str());
    hr = mediaEngine_->SetSource(bstrPath);
    SysFreeString(bstrPath);

    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to set source";
        return false;
    }

    // Start loading
    hr = mediaEngine_->Load();
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to load";
        return false;
    }

    return true;
}

bool TCVideoPlayerImpl::createRenderTexture() {
    if (width_ <= 0 || height_ <= 0) return false;

    // Create render target texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width_;
    desc.Height = height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = d3dDevice_->CreateTexture2D(&desc, nullptr, &renderTexture_);
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create render texture";
        return false;
    }

    // Create staging texture for CPU readback
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = d3dDevice_->CreateTexture2D(&desc, nullptr, &stagingTexture_);
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create staging texture";
        return false;
    }

    // Allocate pixel buffer
    pixels_ = new unsigned char[width_ * height_ * 4];

    return true;
}

bool TCVideoPlayerImpl::transferVideoFrame() {
    if (!mediaEngine_ || !renderTexture_) return false;

    // Check if video has new frame
    LONGLONG pts;
    if (mediaEngine_->OnVideoStreamTick(&pts) != S_OK) {
        return false;
    }

    // Transfer frame to our texture
    MFARGB bgColor = {0, 0, 0, 0};
    MFVideoNormalizedRect srcRect = {0.0f, 0.0f, 1.0f, 1.0f};
    RECT dstRect = {0, 0, width_, height_};

    HRESULT hr = mediaEngine_->TransferVideoFrame(
        renderTexture_.Get(),
        &srcRect,
        &dstRect,
        &bgColor
    );

    if (FAILED(hr)) {
        return false;
    }

    // Copy to staging texture for CPU access
    d3dContext_->CopyResource(stagingTexture_.Get(), renderTexture_.Get());

    // Map and read pixels
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = d3dContext_->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr)) {
        // Copy row by row (handle pitch/stride)
        unsigned char* src = static_cast<unsigned char*>(mapped.pData);
        unsigned char* dst = pixels_;
        int rowBytes = width_ * 4;

        for (int y = 0; y < height_; y++) {
            // BGRA to RGBA conversion
            for (int x = 0; x < width_; x++) {
                int srcIdx = x * 4;
                int dstIdx = x * 4;
                dst[dstIdx + 0] = src[srcIdx + 2]; // R <- B
                dst[dstIdx + 1] = src[srcIdx + 1]; // G <- G
                dst[dstIdx + 2] = src[srcIdx + 0]; // B <- R
                dst[dstIdx + 3] = src[srcIdx + 3]; // A <- A
            }
            src += mapped.RowPitch;
            dst += rowBytes;
        }

        d3dContext_->Unmap(stagingTexture_.Get(), 0);
        return true;
    }

    return false;
}

bool TCVideoPlayerImpl::loadAudioInfo(const std::string& path) {
    // Use IMFSourceReader to get audio track info
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring widePath(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &widePath[0], wideLen);

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, &reader);
    if (FAILED(hr)) {
        return false;
    }

    // Get native audio media type
    ComPtr<IMFMediaType> nativeType;
    hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &nativeType);
    if (FAILED(hr)) {
        // No audio stream
        hasAudio_ = false;
        return true;
    }

    hasAudio_ = true;

    // Get audio format info
    GUID subtype;
    hr = nativeType->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (SUCCEEDED(hr)) {
        audioCodec_ = subtype.Data1;
    }

    UINT32 sampleRate = 0;
    hr = nativeType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
    if (SUCCEEDED(hr)) {
        audioSampleRate_ = sampleRate;
    }

    UINT32 channels = 0;
    hr = nativeType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
    if (SUCCEEDED(hr)) {
        audioChannels_ = channels;
    }

    logNotice("VideoPlayer") << "Audio: " << audioChannels_ << "ch, " << audioSampleRate_ << "Hz";
    return true;
}

std::vector<uint8_t> TCVideoPlayerImpl::getAudioData() const {
    if (!hasAudio_ || mediaPath_.empty()) {
        return {};
    }

    // Use IMFSourceReader to decode audio to PCM
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, mediaPath_.c_str(), -1, nullptr, 0);
    std::wstring widePath(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, mediaPath_.c_str(), -1, &widePath[0], wideLen);

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, &reader);
    if (FAILED(hr)) {
        return {};
    }

    // Configure to output PCM
    ComPtr<IMFMediaType> pcmType;
    hr = MFCreateMediaType(&pcmType);
    if (FAILED(hr)) return {};

    pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pcmType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audioSampleRate_);
    pcmType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audioChannels_);

    hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcmType.Get());
    if (FAILED(hr)) return {};

    // Read all audio samples
    std::vector<uint8_t> audioData;
    audioData.reserve(audioSampleRate_ * audioChannels_ * 2 * 10); // Reserve for ~10 sec

    while (true) {
        ComPtr<IMFSample> sample;
        DWORD flags = 0;

        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            break;
        }

        if (!sample) continue;

        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) continue;

        BYTE* data = nullptr;
        DWORD length = 0;
        hr = buffer->Lock(&data, nullptr, &length);
        if (SUCCEEDED(hr)) {
            audioData.insert(audioData.end(), data, data + length);
            buffer->Unlock();
        }
    }

    return audioData;
}

bool TCVideoPlayerImpl::load(const std::string& path, VideoPlayer* player) {
    if (!InitMediaFoundation()) {
        return false;
    }

    mediaPath_ = path;

    InitializeCriticalSection(&criticalSection_);
    csInitialized_ = true;

    if (!createD3D11Device()) {
        return false;
    }

    if (!createMediaEngine(path)) {
        return false;
    }

    // Wait for metadata to load (synchronous load for now)
    // In production, this should be async with callbacks
    int timeout = 5000; // 5 seconds
    while (!isReady_ && timeout > 0) {
        Sleep(10);
        timeout -= 10;

        // Process pending events
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (!isReady_) {
        logError("VideoPlayer") << "Timeout waiting for video to load";
        return false;
    }

    // Load audio track info
    loadAudioInfo(path);

    isLoaded_ = true;
    return true;
}

void TCVideoPlayerImpl::close() {
    EnterCriticalSection(&criticalSection_);

    if (mediaEngine_) {
        mediaEngine_->Shutdown();
        mediaEngine_.Reset();
    }
    mediaEngineEx_.Reset();

    if (eventNotify_) {
        eventNotify_->Release();
        eventNotify_ = nullptr;
    }

    renderTexture_.Reset();
    stagingTexture_.Reset();
    dxgiManager_.Reset();
    d3dContext_.Reset();
    d3dDevice_.Reset();

    if (pixels_) {
        delete[] pixels_;
        pixels_ = nullptr;
    }

    isLoaded_ = false;
    isReady_ = false;
    hasNewFrame_ = false;
    isFinished_ = false;
    width_ = 0;
    height_ = 0;
    hasAudio_ = false;
    audioCodec_ = 0;
    audioSampleRate_ = 0;
    audioChannels_ = 0;
    mediaPath_.clear();

    LeaveCriticalSection(&criticalSection_);

    if (csInitialized_) {
        DeleteCriticalSection(&criticalSection_);
        csInitialized_ = false;
    }

    CloseMediaFoundation();
}

void TCVideoPlayerImpl::play() {
    if (mediaEngine_) {
        isFinished_ = false;
        mediaEngine_->Play();
    }
}

void TCVideoPlayerImpl::stop() {
    if (mediaEngine_) {
        mediaEngine_->Pause();
        mediaEngine_->SetCurrentTime(0.0);
    }
}

void TCVideoPlayerImpl::setPaused(bool paused) {
    if (mediaEngine_) {
        if (paused) {
            mediaEngine_->Pause();
        } else {
            mediaEngine_->Play();
        }
    }
}

void TCVideoPlayerImpl::update(VideoPlayer* player) {
    hasNewFrame_ = false;

    if (!isLoaded_ || !mediaEngine_) return;

    EnterCriticalSection(&criticalSection_);

    if (transferVideoFrame()) {
        hasNewFrame_ = true;

        // Copy pixels to VideoPlayer's buffer
        if (player && pixels_) {
            unsigned char* playerPixels = player->getPixels();
            if (playerPixels) {
                memcpy(playerPixels, pixels_, width_ * height_ * 4);
            }
        }
    }

    // Check for loop
    if (isLoop_ && isFinished_) {
        mediaEngine_->SetCurrentTime(0.0);
        mediaEngine_->Play();
        isFinished_ = false;
    }

    LeaveCriticalSection(&criticalSection_);
}

float TCVideoPlayerImpl::getPosition() const {
    if (!mediaEngine_) return 0.0f;
    double currentTime = mediaEngine_->GetCurrentTime();
    return (duration_ > 0) ? static_cast<float>(currentTime / duration_) : 0.0f;
}

void TCVideoPlayerImpl::setPosition(float pct) {
    if (mediaEngine_ && duration_ > 0) {
        double seekTime = pct * duration_;
        mediaEngine_->SetCurrentTime(seekTime);
    }
}

float TCVideoPlayerImpl::getDuration() const {
    return duration_;
}

void TCVideoPlayerImpl::setVolume(float vol) {
    if (mediaEngine_) {
        mediaEngine_->SetVolume(vol);
    }
}

void TCVideoPlayerImpl::setSpeed(float speed) {
    if (mediaEngine_) {
        mediaEngine_->SetPlaybackRate(speed);
    }
}

void TCVideoPlayerImpl::setLoop(bool loop) {
    isLoop_ = loop;
    if (mediaEngine_) {
        mediaEngine_->SetLoop(loop ? TRUE : FALSE);
    }
}

int TCVideoPlayerImpl::getCurrentFrame() const {
    if (!mediaEngine_ || frameRate_ <= 0) return 0;
    double currentTime = mediaEngine_->GetCurrentTime();
    return static_cast<int>(currentTime * frameRate_);
}

int TCVideoPlayerImpl::getTotalFrames() const {
    if (frameRate_ <= 0) return 0;
    return static_cast<int>(duration_ * frameRate_);
}

void TCVideoPlayerImpl::setFrame(int frame) {
    if (frameRate_ > 0) {
        double time = frame / frameRate_;
        if (mediaEngine_) {
            mediaEngine_->SetCurrentTime(time);
        }
    }
}

void TCVideoPlayerImpl::nextFrame() {
    if (mediaEngineEx_) {
        mediaEngineEx_->FrameStep(TRUE);
    }
}

void TCVideoPlayerImpl::previousFrame() {
    // Go back one frame
    int currentFrame = getCurrentFrame();
    if (currentFrame > 0) {
        setFrame(currentFrame - 1);
    }
}

void TCVideoPlayerImpl::onMediaEvent(DWORD event, DWORD_PTR param1, DWORD param2) {
    switch (event) {
        case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA: {
            // Get video dimensions
            DWORD w, h;
            if (SUCCEEDED(mediaEngine_->GetNativeVideoSize(&w, &h))) {
                width_ = static_cast<int>(w);
                height_ = static_cast<int>(h);
                logNotice("VideoPlayer") << "Video size: " << width_ << "x" << height_;
            }

            // Get duration
            duration_ = static_cast<float>(mediaEngine_->GetDuration());
            logNotice("VideoPlayer") << "Duration: " << duration_ << " sec";

            // Create render texture
            createRenderTexture();
            break;
        }

        case MF_MEDIA_ENGINE_EVENT_CANPLAY:
        case MF_MEDIA_ENGINE_EVENT_CANPLAYTHROUGH:
            isReady_ = true;
            break;

        case MF_MEDIA_ENGINE_EVENT_ENDED:
            isFinished_ = true;
            break;

        case MF_MEDIA_ENGINE_EVENT_ERROR: {
            MF_MEDIA_ENGINE_ERR err;
            if (mediaEngine_) {
                ComPtr<IMFMediaError> error;
                mediaEngine_->GetError(&error);
                if (error) {
                    err = static_cast<MF_MEDIA_ENGINE_ERR>(error->GetErrorCode());
                    logError("VideoPlayer") << "Media error: " << static_cast<int>(err);
                }
            }
            break;
        }

        default:
            break;
    }
}

// =============================================================================
// VideoPlayer platform methods (Windows implementation)
// =============================================================================

namespace trussc {

bool VideoPlayer::loadPlatform(const std::string& path) {
    auto impl = new TCVideoPlayerImpl();

    if (!impl->load(path, this)) {
        delete impl;
        return false;
    }

    platformHandle_ = impl;
    width_ = impl->getWidth();
    height_ = impl->getHeight();

    // Allocate pixel buffer
    if (width_ > 0 && height_ > 0) {
        pixels_ = new unsigned char[width_ * height_ * 4];
        std::memset(pixels_, 0, width_ * height_ * 4);
    }

    return true;
}

void VideoPlayer::closePlatform() {
    if (platformHandle_) {
        auto impl = static_cast<TCVideoPlayerImpl*>(platformHandle_);
        delete impl;
        platformHandle_ = nullptr;
    }
}

void VideoPlayer::playPlatform() {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->play();
    }
}

void VideoPlayer::stopPlatform() {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->stop();
    }
}

void VideoPlayer::setPausedPlatform(bool paused) {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->setPaused(paused);
    }
}

void VideoPlayer::updatePlatform() {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->update(this);
    }
}

bool VideoPlayer::hasNewFramePlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->hasNewFrame();
    }
    return false;
}

bool VideoPlayer::isFinishedPlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->isFinished();
    }
    return false;
}

float VideoPlayer::getPositionPlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->getPosition();
    }
    return 0.0f;
}

void VideoPlayer::setPositionPlatform(float pct) {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->setPosition(pct);
    }
}

float VideoPlayer::getDurationPlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->getDuration();
    }
    return 0.0f;
}

void VideoPlayer::setVolumePlatform(float vol) {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->setVolume(vol);
    }
}

void VideoPlayer::setSpeedPlatform(float speed) {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->setSpeed(speed);
    }
}

void VideoPlayer::setLoopPlatform(bool loop) {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->setLoop(loop);
    }
}

int VideoPlayer::getCurrentFramePlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->getCurrentFrame();
    }
    return 0;
}

int VideoPlayer::getTotalFramesPlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->getTotalFrames();
    }
    return 0;
}

void VideoPlayer::setFramePlatform(int frame) {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->setFrame(frame);
    }
}

void VideoPlayer::nextFramePlatform() {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->nextFrame();
    }
}

void VideoPlayer::previousFramePlatform() {
    if (platformHandle_) {
        static_cast<TCVideoPlayerImpl*>(platformHandle_)->previousFrame();
    }
}


// Audio track support
bool VideoPlayer::hasAudioPlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->hasAudio();
    }
    return false;
}

uint32_t VideoPlayer::getAudioCodecPlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->getAudioCodec();
    }
    return 0;
}

int VideoPlayer::getAudioSampleRatePlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->getAudioSampleRate();
    }
    return 0;
}

int VideoPlayer::getAudioChannelsPlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->getAudioChannels();
    }
    return 0;
}

std::vector<uint8_t> VideoPlayer::getAudioDataPlatform() const {
    if (platformHandle_) {
        return static_cast<TCVideoPlayerImpl*>(platformHandle_)->getAudioData();
    }
    return {};
}

} // namespace trussc

#endif // _WIN32
