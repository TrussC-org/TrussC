// =============================================================================
// tcVideoPlayer_win.cpp - Windows VideoPlayer implementation using Media Foundation
// =============================================================================
// Uses IMFMediaEngine for hardware-accelerated video decoding.
// Each new frame is read back through a D3D11 staging texture and converted
// from BGRA to RGBA on the CPU, then copied to VideoPlayer's pixel buffer.
// VideoPlayer::update() uploads that buffer to a sokol_gfx texture.
//
// Reference: openFrameworks ofMediaFoundationPlayer (MIT License)
// Based on code by Andrew Wright (https://github.com/axjxwright/AX-MediaPlayer/)
// =============================================================================

#ifdef _WIN32

#include "TrussC.h"
#include <vector>
#include <chrono>
#include "tc/video/internal/tcVideoNotifyQueue.h"

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
struct VideoPlayerState;

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
    MediaEngineNotify(const std::shared_ptr<VideoPlayerState>& state) : state_(state), refCount_(1) {}

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
    std::weak_ptr<VideoPlayerState> state_;
    LONG refCount_;
};

struct VideoPlayerState {
    std::shared_ptr<internal::VideoDecodeStatus> status_ = std::make_shared<internal::VideoDecodeStatus>();
    internal::VideoNotifyQueue notifications;
    internal::VideoErrorQueue playbackErrors_;
    bool mfInitialized = false;
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
    float frameRate_ = 0.0f;
    bool isLoaded_ = false;
    bool isReady_ = false;
    bool hasNewFrame_ = false;
    bool isFinished_ = false;
    bool isLoop_ = false;

    // Pixel buffer for CPU readback
    std::vector<unsigned char> pixels_;

    // Audio track info
    bool hasAudio_ = false;
    uint32_t audioCodec_ = 0;
    int audioSampleRate_ = 0;
    int audioChannels_ = 0;
    std::string mediaPath_;

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

    bool hasNewFrame() const { return state_->hasNewFrame_; }
    bool isFinished() const { return state_->isFinished_; }

    float getPosition() const;
    void setPosition(float pct);
    float getDuration() const;

    void setVolume(float vol);
    void setSpeed(float speed);
    void setLoop(bool loop);

    float getFrameRate() const { return static_cast<float>(state_->frameRate_); }
    int getCurrentFrame() const;
    int getTotalFrames() const;
    void setFrame(int frame);
    void nextFrame();
    void previousFrame();

    // Event handler (called by the main-thread queue drain)
    void onMediaEvent(DWORD event, DWORD_PTR param1, DWORD param2);

    std::shared_ptr<internal::VideoDecodeStatus> status() const { return state_->status_; }
    int getWidth() const { return state_->width_; }
    int getHeight() const { return state_->height_; }

    // Audio track info
    bool hasAudio() const { return state_->hasAudio_; }
    uint32_t getAudioCodec() const { return state_->audioCodec_; }
    int getAudioSampleRate() const { return state_->audioSampleRate_; }
    int getAudioChannels() const { return state_->audioChannels_; }
    std::vector<uint8_t> getAudioData() const;

private:
    bool createD3D11Device();
    bool createMediaEngine(const std::string& path);
    bool createRenderTexture();
    bool transferVideoFrame();
    bool loadMediaInfo(const std::string& path);

    std::shared_ptr<VideoPlayerState> state_ = std::make_shared<VideoPlayerState>();
    void drainNotifications();

};

// =============================================================================
// MediaEngineNotify::EventNotify implementation
// =============================================================================
STDMETHODIMP MediaEngineNotify::EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) {
    internal::enqueueVideoNotify(state_, {event, param1, param2});
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

    // Try progressively relaxed flags until one succeeds
    struct { UINT flags; const char* desc; } attempts[] = {
        { D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
          "VIDEO_SUPPORT + DEBUG" },
        { D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
          "VIDEO_SUPPORT" },
        { D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
          "BGRA + DEBUG" },
        { D3D11_CREATE_DEVICE_BGRA_SUPPORT,
          "BGRA only" },
    };

    HRESULT hr = E_FAIL;
    for (auto& a : attempts) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            a.flags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &state_->d3dDevice_,
            &featureLevel,
            &state_->d3dContext_
        );
        if (SUCCEEDED(hr)) {
            logNotice("VideoPlayer") << "D3D11 device created with: " << a.desc;
            break;
        }
        logWarning("VideoPlayer") << "D3D11 failed with: " << a.desc;
    }

    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create D3D11 device (all attempts failed)";
        return false;
    }

    // Enable multi-threading
    ComPtr<ID3D10Multithread> multithread;
    if (SUCCEEDED(state_->d3dDevice_.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }

    // Create DXGI device manager
    hr = MFCreateDXGIDeviceManager(&state_->dxgiResetToken_, &state_->dxgiManager_);
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create DXGI device manager";
        return false;
    }

    hr = state_->dxgiManager_->ResetDevice(state_->d3dDevice_.Get(), state_->dxgiResetToken_);
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
    hr = attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, state_->dxgiManager_.Get());
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to set DXGI manager";
        return false;
    }

    // Set callback
    state_->eventNotify_ = new MediaEngineNotify(state_);
    hr = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, state_->eventNotify_);
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
        &state_->mediaEngine_
    );

    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create MediaEngine";
        return false;
    }

    // Get extended interface
    state_->mediaEngine_.As(&state_->mediaEngineEx_);

    // Disable autoplay
    state_->mediaEngine_->SetAutoPlay(FALSE);

    // Convert path to wide string and BSTR
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring widePath(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &widePath[0], wideLen);

    BSTR bstrPath = SysAllocString(widePath.c_str());
    hr = state_->mediaEngine_->SetSource(bstrPath);
    SysFreeString(bstrPath);

    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to set source";
        return false;
    }

    // Start loading
    hr = state_->mediaEngine_->Load();
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to load";
        return false;
    }

    return true;
}

bool TCVideoPlayerImpl::createRenderTexture() {
    size_t bytes = 0;
    if (!internal::VideoDecodeLimits::rgba(state_->width_, state_->height_, bytes)) {
        state_->status_->reject(bytes, 0);
        state_->status_->warn(); return false;
    }
    state_->renderTexture_.Reset();
    state_->stagingTexture_.Reset();
    std::vector<unsigned char>().swap(state_->pixels_);

    // Create render target texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = state_->width_;
    desc.Height = state_->height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = state_->d3dDevice_->CreateTexture2D(&desc, nullptr, &state_->renderTexture_);
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create render texture";
        return false;
    }

    // Create staging texture for CPU readback
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = state_->d3dDevice_->CreateTexture2D(&desc, nullptr, &state_->stagingTexture_);
    if (FAILED(hr)) {
        logError("VideoPlayer") << "Failed to create staging texture";
        return false;
    }

    // Allocate pixel buffer
    try { state_->pixels_.assign(bytes, 0); }
    catch (const std::exception&) {
        state_->status_->reject(bytes, 0); return false;
    }

    return true;
}

bool TCVideoPlayerImpl::transferVideoFrame() {
    if (!state_->mediaEngine_ || !state_->renderTexture_) return false;

    // Check if video has new frame
    LONGLONG pts;
    if (state_->mediaEngine_->OnVideoStreamTick(&pts) != S_OK) {
        return false;
    }

    DWORD actualWidth = 0, actualHeight = 0;
    size_t actualBytes = 0;
    if (FAILED(state_->mediaEngine_->GetNativeVideoSize(&actualWidth, &actualHeight)) ||
        !internal::VideoDecodeLimits::rgba(actualWidth, actualHeight, actualBytes)) {
        state_->status_->reject(actualBytes, 0);
        state_->playbackErrors_.report("Video frame dimensions exceed decode limits", E_INVALIDARG);
        return false;
    }
    // Transfer frame to our texture
    MFARGB bgColor = {0, 0, 0, 0};
    MFVideoNormalizedRect srcRect = {0.0f, 0.0f, 1.0f, 1.0f};
    RECT dstRect = {0, 0, state_->width_, state_->height_};

    HRESULT hr = state_->mediaEngine_->TransferVideoFrame(
        state_->renderTexture_.Get(),
        &srcRect,
        &dstRect,
        &bgColor
    );

    if (FAILED(hr)) {
        HRESULT reason = state_->d3dDevice_->GetDeviceRemovedReason();
        if (FAILED(reason)) state_->playbackErrors_.report("D3D11 video device lost", reason);
        return false;
    }

    // Copy to staging texture for CPU access
    state_->d3dContext_->CopyResource(state_->stagingTexture_.Get(), state_->renderTexture_.Get());

    // Map and read pixels
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = state_->d3dContext_->Map(state_->stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr)) {
        if (!mapped.pData || !internal::VideoDecodeLimits::rows(size_t(state_->width_), size_t(state_->height_),
                mapped.RowPitch, size_t(mapped.RowPitch) * size_t(state_->height_))) {
            state_->status_->reject(mapped.RowPitch, size_t(mapped.RowPitch) * size_t(state_->height_));
            state_->d3dContext_->Unmap(state_->stagingTexture_.Get(), 0);
            state_->playbackErrors_.report("Video frame stride exceeds decode limits", E_INVALIDARG);
            return false;
        }
        // Copy row by row (handle pitch/stride)
        unsigned char* src = static_cast<unsigned char*>(mapped.pData);
        unsigned char* dst = state_->pixels_.data();
        int rowBytes = state_->width_ * 4;

        for (int y = 0; y < state_->height_; y++) {
            // BGRA to RGBA conversion
            for (int x = 0; x < state_->width_; x++) {
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

        state_->d3dContext_->Unmap(state_->stagingTexture_.Get(), 0);
        return true;
    }

    HRESULT reason = state_->d3dDevice_->GetDeviceRemovedReason();
    if (FAILED(reason)) state_->playbackErrors_.report("D3D11 video device lost", reason);
    return false;
}

bool TCVideoPlayerImpl::loadMediaInfo(const std::string& path) {
    state_->frameRate_ = 0.0f;
    // Reuse one source reader for video frame rate and audio track info.
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring widePath(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &widePath[0], wideLen);

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, &reader);
    if (FAILED(hr)) {
        return false;
    }

    // Read video metadata before probing audio: silent videos need this too.
    ComPtr<IMFMediaType> videoType;
    UINT32 numerator = 0, denominator = 0;
    // Known issue (see #697): for Matroska sources Media Foundation reports
    // half the real rate in MF_MT_FRAME_RATE, while IMFSample::GetSampleDuration
    // is correct there. Only the frame-index APIs (getFrameRate, frame
    // stepping) are affected; playback is paced by MediaEngine.
    UINT32 metadataWidth = 0, metadataHeight = 0;
    size_t metadataBytes = 0;
    // Some MediaEngine sources expose metadata only after loading. Admit
    // known dimensions here; the engine's output boundary checks them again.
    if (SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &videoType))) {
        if (SUCCEEDED(MFGetAttributeSize(videoType.Get(), MF_MT_FRAME_SIZE, &metadataWidth, &metadataHeight)) &&
            (metadataWidth && metadataHeight && !internal::VideoDecodeLimits::rgba(metadataWidth, metadataHeight, metadataBytes))) {
            state_->status_->reject(metadataBytes, 0);
            return false;
        }
        if (SUCCEEDED(MFGetAttributeRatio(videoType.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) &&
            numerator > 0 && denominator > 0)
            state_->frameRate_ = static_cast<float>(numerator) / denominator;
    }

    // Get native audio media type
    ComPtr<IMFMediaType> nativeType;
    hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &nativeType);
    if (FAILED(hr)) {
        // No audio stream
        state_->hasAudio_ = false;
        return true;
    }

    state_->hasAudio_ = true;

    // Get audio format info
    GUID subtype;
    hr = nativeType->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (SUCCEEDED(hr)) {
        state_->audioCodec_ = subtype.Data1;
    }

    UINT32 sampleRate = 0;
    hr = nativeType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
    if (SUCCEEDED(hr)) {
        state_->audioSampleRate_ = sampleRate;
    }

    UINT32 channels = 0;
    hr = nativeType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
    if (SUCCEEDED(hr)) {
        state_->audioChannels_ = channels;
    }

    logNotice("VideoPlayer") << "Audio: " << state_->audioChannels_ << "ch, " << state_->audioSampleRate_ << "Hz";
    return true;
}

std::vector<uint8_t> TCVideoPlayerImpl::getAudioData() const {
    const auto state = state_;
    if (!state->hasAudio_ || state->mediaPath_.empty()) {
        return {};
    }

    // Use IMFSourceReader to read raw audio data (passthrough, no decoding)
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, state->mediaPath_.c_str(), -1, nullptr, 0);
    std::wstring widePath(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, state->mediaPath_.c_str(), -1, &widePath[0], wideLen);

    // Create attributes to disable format conversion (passthrough mode)
    ComPtr<IMFAttributes> attrs;
    HRESULT hr = MFCreateAttributes(&attrs, 1);
    if (FAILED(hr)) {
        return {};
    }
    // MF_READWRITE_DISABLE_CONVERTERS = TRUE means no decoding, raw compressed data
    attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);

    ComPtr<IMFSourceReader> reader;
    hr = MFCreateSourceReaderFromURL(widePath.c_str(), attrs.Get(), &reader);
    if (FAILED(hr)) {
        return {};
    }

    // Check if AAC (MFAudioFormat_AAC Data1 = 0x1610)
    // Also check for 'mp4a' FourCC which we store
    bool isAAC = (state->audioCodec_ == 0x1610 ||      // WAVE_FORMAT_MPEG_HEAAC
                  state->audioCodec_ == 0x6D703461 ||  // 'mp4a'
                  state->audioCodec_ == 0x61616320);   // 'aac '

    logNotice("VideoPlayer") << "Extracting audio: codec=0x" << std::hex << state->audioCodec_
                             << " isAAC=" << isAAC << std::dec;

    // Read all audio samples
    std::vector<uint8_t> audioData;
    internal::VideoAudioStorage storage(audioData, *state->status_);
    struct Warning { internal::VideoDecodeStatus& status; ~Warning() { status.warn(); } } warning{*state->status_};

    while (true) {
        ComPtr<IMFSample> sample;
        DWORD flags = 0;

        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr)) return {};
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;

        if (!sample) continue;

        DWORD sampleBytes = 0;
        if (FAILED(sample->GetTotalLength(&sampleBytes)) ||
            sampleBytes > internal::VideoDecodeLimits::inputBytes ||
            (isAAC && sampleBytes > internal::VideoDecodeLimits::adtsPayloadBytes)) {
            state->status_->reject(sampleBytes, internal::VideoDecodeLimits::inputBytes);
            return {};
        }
        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) return {};

        BYTE* data = nullptr;
        DWORD length = 0;
        hr = buffer->Lock(&data, nullptr, &length);
        if (SUCCEEDED(hr)) {
            if (length != sampleBytes) { buffer->Unlock(); return {}; }
            if (isAAC) {
                // AAC: add ADTS header for each frame
                uint8_t adtsHeader[7];
                SoundBuffer::createAdtsHeader(adtsHeader, static_cast<int>(length),
                                               state->audioSampleRate_, state->audioChannels_);
                if (!storage.append(adtsHeader, 7)) {
                    buffer->Unlock(); return {};
                }
            }
            if (length != sampleBytes || !storage.append(data, length)) {
                buffer->Unlock(); return {};
            }
            buffer->Unlock();
        } else { return {}; }
    }

    logNotice("VideoPlayer") << "Extracted " << audioData.size() << " bytes of audio data";
    return audioData;
}

bool TCVideoPlayerImpl::load(const std::string& path, VideoPlayer* player) {
    if (!InitMediaFoundation()) {
        return false;
    }

    state_->mediaPath_ = path;

    state_->mfInitialized = true;

    if (!createD3D11Device()) {
        return false;
    }

    loadMediaInfo(path);
    if (state_->status_->rejected() != 0) return false;

    if (!createMediaEngine(path)) {
        return false;
    }

    // Wait for metadata to load (synchronous load for now)
    // In production, this should be async with callbacks
    // Bound (platform): metadata readiness waits at most 5 s (steady clock). Overflow fails load() and logs one Error.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!state_->isReady_ && state_->status_->rejected() == 0 && std::chrono::steady_clock::now() < deadline) {
        Sleep(10);

        drainNotifications();

        // Process pending events
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (!state_->isReady_ || state_->pixels_.empty()) {
        logError("VideoPlayer") << "Timeout waiting for video to load";
        return false;
    }


    state_->isLoaded_ = true;
    return true;
}

void TCVideoPlayerImpl::close() {
    if (!state_) return;
    state_->notifications.close();

    if (state_->mediaEngine_) {
        state_->mediaEngine_->Shutdown();
        state_->mediaEngine_.Reset();
    }
    state_->mediaEngineEx_.Reset();

    if (state_->eventNotify_) {
        state_->eventNotify_->Release();
        state_->eventNotify_ = nullptr;
    }

    state_->renderTexture_.Reset();
    state_->stagingTexture_.Reset();
    state_->dxgiManager_.Reset();
    state_->d3dContext_.Reset();
    state_->d3dDevice_.Reset();

    state_->pixels_.clear();

    state_->isLoaded_ = false;
    state_->isReady_ = false;
    state_->hasNewFrame_ = false;
    state_->isFinished_ = false;
    state_->width_ = 0;
    state_->height_ = 0;
    state_->frameRate_ = 0.0f;
    state_->hasAudio_ = false;
    state_->audioCodec_ = 0;
    state_->audioSampleRate_ = 0;
    state_->audioChannels_ = 0;
    state_->mediaPath_.clear();


    if (state_->mfInitialized) CloseMediaFoundation();
    state_ = std::make_shared<VideoPlayerState>();  // never null, like VideoGrabber::close()
}

void TCVideoPlayerImpl::play() {
    if (state_->mediaEngine_) {
        state_->isFinished_ = false;
        state_->mediaEngine_->Play();
    }
}

void TCVideoPlayerImpl::stop() {
    if (state_->mediaEngine_) {
        state_->mediaEngine_->Pause();
        state_->mediaEngine_->SetCurrentTime(0.0);
    }
}

void TCVideoPlayerImpl::setPaused(bool paused) {
    if (state_->mediaEngine_) {
        if (paused) {
            state_->mediaEngine_->Pause();
        } else {
            state_->mediaEngine_->Play();
        }
    }
}

void TCVideoPlayerImpl::drainNotifications() {
    for (const auto& event : state_->notifications.take())
        onMediaEvent(event.event, event.param1, event.param2);
}

void TCVideoPlayerImpl::update(VideoPlayer* player) {
    drainNotifications();
    state_->hasNewFrame_ = false;

    if (!state_->isLoaded_ || !state_->mediaEngine_) return;

    if (state_->d3dDevice_) {
        HRESULT reason = state_->d3dDevice_->GetDeviceRemovedReason();
        if (FAILED(reason)) state_->playbackErrors_.report("D3D11 video device lost", reason);
    }
    auto error = state_->playbackErrors_.take();
    if (!error.message.empty()) {
        if (player) internal::VideoPlayerPlatformAccess::reportError(*player, error);
        return;
    }


    if (transferVideoFrame()) {
        state_->hasNewFrame_ = true;

        // Copy pixels to VideoPlayer's buffer
        if (player && !state_->pixels_.empty()) {
            unsigned char* playerPixels = player->getPixels();
            if (playerPixels) {
                memcpy(playerPixels, state_->pixels_.data(), size_t(state_->width_) * state_->height_ * 4);
            }
        }
    }

    // Check for loop
    if (state_->isLoop_ && state_->isFinished_) {
        state_->mediaEngine_->SetCurrentTime(0.0);
        state_->mediaEngine_->Play();
        state_->isFinished_ = false;
    }

}

float TCVideoPlayerImpl::getPosition() const {
    if (!state_->mediaEngine_) return 0.0f;
    double currentTime = state_->mediaEngine_->GetCurrentTime();
    return (state_->duration_ > 0) ? static_cast<float>(currentTime / state_->duration_) : 0.0f;
}

void TCVideoPlayerImpl::setPosition(float pct) {
    if (state_->mediaEngine_ && state_->duration_ > 0) {
        double seekTime = pct * state_->duration_;
        state_->mediaEngine_->SetCurrentTime(seekTime);
    }
}

float TCVideoPlayerImpl::getDuration() const {
    return state_->duration_;
}

void TCVideoPlayerImpl::setVolume(float vol) {
    if (state_->mediaEngine_) {
        state_->mediaEngine_->SetVolume(vol);
    }
}

void TCVideoPlayerImpl::setSpeed(float speed) {
    if (state_->mediaEngine_) {
        state_->mediaEngine_->SetPlaybackRate(speed);
    }
}

void TCVideoPlayerImpl::setLoop(bool loop) {
    state_->isLoop_ = loop;
    if (state_->mediaEngine_) {
        state_->mediaEngine_->SetLoop(loop ? TRUE : FALSE);
    }
}

int TCVideoPlayerImpl::getCurrentFrame() const {
    if (!state_->mediaEngine_ || state_->frameRate_ <= 0) return 0;
    double currentTime = state_->mediaEngine_->GetCurrentTime();
    return static_cast<int>(currentTime * state_->frameRate_);
}

int TCVideoPlayerImpl::getTotalFrames() const {
    if (state_->frameRate_ <= 0) return 0;
    return static_cast<int>(state_->duration_ * state_->frameRate_);
}

void TCVideoPlayerImpl::setFrame(int frame) {
    if (state_->frameRate_ > 0) {
        double time = frame / state_->frameRate_;
        if (state_->mediaEngine_) {
            state_->mediaEngine_->SetCurrentTime(time);
        }
    }
}

void TCVideoPlayerImpl::nextFrame() {
    if (state_->mediaEngineEx_) {
        state_->mediaEngineEx_->FrameStep(TRUE);
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
            size_t bytes = 0;
            if (SUCCEEDED(state_->mediaEngine_->GetNativeVideoSize(&w, &h)) &&
                internal::VideoDecodeLimits::rgba(w, h, bytes)) {
                if (state_->pixels_.empty()) {
                    state_->width_ = static_cast<int>(w);
                    state_->height_ = static_cast<int>(h);
                }
                logNotice("VideoPlayer") << "Video size: " << state_->width_ << "x" << state_->height_;
            }

            else {
                state_->status_->reject(bytes, 0);
                state_->status_->warn();
                state_->playbackErrors_.report("Video metadata exceeds decode limits", E_INVALIDARG);
                break;
            }
            // Get duration
            state_->duration_ = static_cast<float>(state_->mediaEngine_->GetDuration());
            logNotice("VideoPlayer") << "Duration: " << state_->duration_ << " sec";

            // Create render texture
            if (state_->pixels_.empty()) createRenderTexture();
            break;
        }

        case MF_MEDIA_ENGINE_EVENT_CANPLAY:
        case MF_MEDIA_ENGINE_EVENT_CANPLAYTHROUGH:
            state_->isReady_ = true;
            break;

        case MF_MEDIA_ENGINE_EVENT_ENDED:
            state_->isFinished_ = true;
            break;

        case MF_MEDIA_ENGINE_EVENT_ERROR:
            state_->playbackErrors_.report("Media Foundation playback error (MF_MEDIA_ENGINE_ERR " +
                                   std::to_string(param1) + ")",
                                   param2 ? static_cast<int64_t>(static_cast<HRESULT>(param2))
                                          : static_cast<int64_t>(param1));
            break;

        case MF_MEDIA_ENGINE_EVENT_RESOURCELOST:
            state_->playbackErrors_.report("Media Foundation video resource lost");
            break;

        default:
            break;
    }
}

// =============================================================================
// VideoPlayer platform methods (Windows implementation)
// =============================================================================

namespace trussc {

bool VideoPlayer::loadPlatform(const fs::path& path) {
    auto owned = std::make_unique<TCVideoPlayerImpl>();
    auto* impl = owned.get();
    decodeStatus_ = impl->status();

    // Impl-side strings are UTF-8 (converted to wide via CP_UTF8 internally)
    if (!impl->load(internal::pathToUtf8(path), this)) {
        return false;
    }

    platformHandle_ = owned.release();
    width_ = impl->getWidth();
    height_ = impl->getHeight();

    // Allocate pixel buffer
    if (width_ > 0 && height_ > 0) {
        pixels_ = new (std::nothrow) unsigned char[size_t(width_) * height_ * 4];
        if (!pixels_) { closePlatform(); return false; }
        std::memset(pixels_, 0, size_t(width_) * height_ * 4);
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

float VideoPlayer::getFrameRatePlatform() const {
    if (!platformHandle_) return 0.0f;
    return static_cast<TCVideoPlayerImpl*>(platformHandle_)->getFrameRate();
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

// =============================================================================
// Hardware acceleration info
// =============================================================================
// Media Foundation is configured with a DXGI device manager backed by a D3D11
// device created with D3D11_CREATE_DEVICE_VIDEO_SUPPORT, so decoding runs on
// the GPU via D3D11VA whenever the codec is supported. IMFMediaEngine does not
// expose a direct "is HW active" query, so we report "mediafoundation" for any
// successfully loaded video.

bool VideoPlayer::isUsingHwAccelPlatform() const {
    return platformHandle_ != nullptr;
}

std::string VideoPlayer::getHwAccelNamePlatform() const {
    return platformHandle_ ? "mediafoundation" : "none";
}

// Shared single-frame decode via IMFSourceReader (independent of the playback
// IMFMediaEngine path; runs on the calling thread, no GPU/D3D needed).
//
// When `exact` is true we seek to the preceding keyframe then read forward
// until the sample timestamp reaches timeSec, yielding the exact frame shown at
// that time. When false we return the first sample after the seek — the nearest
// keyframe at or before timeSec — which is faster but time-approximate.
static bool tcv_extract_frame_win(const std::string& path, Pixels& outPixels,
                                  float timeSec, float* outDuration, bool exact,
                                  std::shared_ptr<internal::VideoDecodeStatus> status) {
    if (!status) status = std::make_shared<internal::VideoDecodeStatus>();
    struct Warning { internal::VideoDecodeStatus& status; ~Warning() { status.warn(); } } warning{*status};
    if (!InitMediaFoundation()) {
        return false;
    }

    bool ok = false;
    Pixels decoded;
    {
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        std::wstring widePath(wideLen, 0);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &widePath[0], wideLen);

        // Enable the built-in video processor so we can request RGB32 output
        // regardless of the source pixel format.
        ComPtr<IMFAttributes> attrs;
        HRESULT hr = MFCreateAttributes(&attrs, 1);
        if (SUCCEEDED(hr)) {
            attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        }

        ComPtr<IMFSourceReader> reader;
        if (SUCCEEDED(hr)) {
            hr = MFCreateSourceReaderFromURL(widePath.c_str(), attrs.Get(), &reader);
        }

        // Select the first video stream only.
        if (SUCCEEDED(hr)) {
            reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
            hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        }

        if (SUCCEEDED(hr)) {
            ComPtr<IMFMediaType> nativeType;
            UINT32 nativeWidth = 0, nativeHeight = 0;
            size_t bytes = 0;
            if (SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType)) &&
                SUCCEEDED(MFGetAttributeSize(nativeType.Get(), MF_MT_FRAME_SIZE, &nativeWidth, &nativeHeight)) &&
                (nativeWidth && nativeHeight && !internal::VideoDecodeLimits::rgba(nativeWidth, nativeHeight, bytes))) {
                status->reject(bytes, 0); hr = E_INVALIDARG;
            }
        }

        // Ask for uncompressed RGB32 (BGRA in memory).
        // Media Foundation yields no H.264 frames at coded width 32; width 34 (coded 48) works.
        if (SUCCEEDED(hr)) {
            ComPtr<IMFMediaType> rgbType;
            hr = MFCreateMediaType(&rgbType);
            if (SUCCEEDED(hr)) {
                rgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                rgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
                hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                 nullptr, rgbType.Get());
            }
        }

        // Duration (100ns units) for the optional out-param + clamping.
        float duration = 0.0f;
        if (SUCCEEDED(hr)) {
            PROPVARIANT var;
            PropVariantInit(&var);
            if (SUCCEEDED(reader->GetPresentationAttribute(
                    MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) &&
                var.vt == VT_UI8) {
                duration = (float)(var.uhVal.QuadPart / 10000000.0);
            }
            PropVariantClear(&var);
        }
        if (outDuration) *outDuration = duration;

        // Clamp requested time to valid range (mirrors the macOS behaviour).
        if (duration > 0.0f && timeSec > duration) timeSec = duration * 0.1f;
        if (timeSec < 0) timeSec = 0;

        // Seek to the requested position (nearest keyframe at or before it).
        if (SUCCEEDED(hr) && timeSec > 0.0f) {
            PROPVARIANT pos;
            PropVariantInit(&pos);
            pos.vt = VT_I8;
            pos.hVal.QuadPart = (LONGLONG)(timeSec * 10000000.0);
            reader->SetCurrentPosition(GUID_NULL, pos);
            PropVariantClear(&pos);
        }

        // Read samples until we reach the target. In keyframe mode we stop at
        // the first decoded sample after the seek. In exact mode we keep the
        // latest decoded sample and read forward until its timestamp reaches
        // timeSec (the frame actually displayed at that time).
        const LONGLONG targetTicks = (LONGLONG)(timeSec * 10000000.0);
        ComPtr<IMFSample> sample;
        while (SUCCEEDED(hr)) {
            ComPtr<IMFMediaType> frameType;
            UINT32 frameWidth = 0, frameHeight = 0;
            size_t frameBytes = 0;
            if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &frameType)) &&
                SUCCEEDED(MFGetAttributeSize(frameType.Get(), MF_MT_FRAME_SIZE, &frameWidth, &frameHeight)) &&
                (frameWidth && frameHeight && !internal::VideoDecodeLimits::rgba(frameWidth, frameHeight, frameBytes))) {
                status->reject(frameBytes, 0); hr = E_INVALIDARG; break;
            }
            DWORD flags = 0;
            LONGLONG llTimestamp = 0;
            ComPtr<IMFSample> s;
            hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                    nullptr, &flags, &llTimestamp, &s);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                break;  // fall back to the last sample we retained, if any
            }
            if (!s) {
                // No sample this iteration (e.g. a stream/format tick) — keep reading.
                continue;
            }
            sample = s;
            if (!exact || llTimestamp >= targetTicks) {
                break;  // keyframe: first sample; exact: reached the target time
            }
        }

        // Resolve the actual output frame size and stride (may have changed
        // during the read).
        UINT32 width = 0, height = 0;
        LONG stride = 0;
        if (SUCCEEDED(hr) && sample) {
            ComPtr<IMFMediaType> curType;
            if (SUCCEEDED(reader->GetCurrentMediaType(
                    MF_SOURCE_READER_FIRST_VIDEO_STREAM, &curType))) {
                MFGetAttributeSize(curType.Get(), MF_MT_FRAME_SIZE, &width, &height);
                UINT32 mfStride = 0;
                if (SUCCEEDED(curType->GetUINT32(MF_MT_DEFAULT_STRIDE, &mfStride))) {
                    stride = (LONG)(INT32)mfStride;  // negative => bottom-up
                } else {
                    if (width <= uint32_t((std::numeric_limits<LONG>::max)() / 4)) stride = LONG(width * 4);
                    else { status->reject(width, (std::numeric_limits<LONG>::max)() / 4); hr = E_INVALIDARG; }
                }
            }
        }

        size_t outputBytes = 0;
        if (SUCCEEDED(hr) && sample && !internal::VideoDecodeLimits::rgba(width, height, outputBytes)) {
            status->reject(outputBytes, 0); hr = E_INVALIDARG;
        }
        if (SUCCEEDED(hr) && sample && width > 0 && height > 0) {
            DWORD sampleBytes = 0;
            if (FAILED(sample->GetTotalLength(&sampleBytes))) {
                status->reject(sampleBytes, 0); hr = E_INVALIDARG;
            }
            ComPtr<IMFMediaBuffer> buffer;
            if (SUCCEEDED(hr)) hr = sample->ConvertToContiguousBuffer(&buffer);
            if (SUCCEEDED(hr)) {
                BYTE* data = nullptr;
                DWORD length = 0;
                hr = buffer->Lock(&data, nullptr, &length);
                if (SUCCEEDED(hr)) {
                    const size_t absStride = size_t(stride < 0 ? -int64_t(stride) : int64_t(stride));
                    if (!data || !internal::VideoDecodeLimits::rows(width, height, absStride, length)) {
                        status->reject(length, 0);
                        buffer->Unlock();
                    } else {
                        try { decoded.allocate((int)width, (int)height, 4); }
                        catch (const std::bad_alloc&) {
                            status->reject(outputBytes, 0);
                            decoded.clear();
                        }
                        if (decoded.getData()) {
                        unsigned char* dst = decoded.getData();
                        int rowBytes = (int)width * 4;

                        for (UINT32 y = 0; y < height; y++) {
                            // Negative stride means the image is stored bottom-up.
                            UINT32 srcRow = stride < 0 ? (height - 1 - y) : y;
                            const BYTE* src = data + (size_t)srcRow * absStride;
                            // BGRA to RGBA conversion
                            for (UINT32 x = 0; x < width; x++) {
                                int srcIdx = x * 4;
                                int dstIdx = x * 4;
                                dst[dstIdx + 0] = src[srcIdx + 2]; // R <- B
                                dst[dstIdx + 1] = src[srcIdx + 1]; // G <- G
                                dst[dstIdx + 2] = src[srcIdx + 0]; // B <- R
                                // RGB32 from the source reader is BGRX: the 4th byte
                                // is undefined padding, not alpha. Force opaque so the
                                // extracted frame isn't fully transparent.
                                dst[dstIdx + 3] = 255;             // A = opaque
                            }
                            dst += rowBytes;
                        }

                        buffer->Unlock();
                        ok = true;
                        } else { buffer->Unlock(); }
                    }
                }
            }
        }
    }

    CloseMediaFoundation();
    if (ok) outPixels = std::move(decoded);
    return ok;
}

bool VideoPlayer::extractFramePlatform(const fs::path& path, Pixels& outPixels,
                                       float timeSec, float* outDuration, std::shared_ptr<internal::VideoDecodeStatus> status) {
    return tcv_extract_frame_win(internal::pathToUtf8(path), outPixels, timeSec, outDuration, /*exact=*/true, status);
}

bool VideoPlayer::extractKeyFramePlatform(const fs::path& path, Pixels& outPixels,
                                          float timeSec, float* outDuration, std::shared_ptr<internal::VideoDecodeStatus> status) {
    if (!status) status = std::make_shared<internal::VideoDecodeStatus>();
    const uint64_t previousRejections = status->rejected();
    std::string p = internal::pathToUtf8(path);
    if (tcv_extract_frame_win(p, outPixels, timeSec, outDuration, /*exact=*/false, status)) {
        return true;
    }
    // No keyframe reachable — fall back to an exact decode.
    if (status->rejected() != previousRejections) return false;
    return tcv_extract_frame_win(p, outPixels, timeSec, outDuration, /*exact=*/true, status);
}

} // namespace trussc

#endif // _WIN32
