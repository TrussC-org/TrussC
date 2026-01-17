#pragma once

#include <TrussC.h>
#include <tcxTcv.h>
#include <tcxHapPlayer.h>
#include <thread>
#include <memory>

using namespace std;
using namespace tc;
using namespace tcx;

// EncodingSession - Manages one video encoding job

class EncodingSession {
public:
    struct Settings {
        string inputPath;
        string outputPath;
        int quality = 1;      // 0=fast, 1=balanced, 2=high
        int partitions = -1;  // -1 = use quality preset
        int uber = -1;        // -1 = use quality preset
        int jobs = 0;         // 0 = auto
        // Compression options
        bool forceAllIFrames = false;
        bool enableSkip = true;
    };

    // Begin encoding with given settings
    bool begin(const Settings& settings);

    // Update encoding - call every frame
    void update();

    // Draw preview and progress bar
    void draw(float x, float y, float maxW, float maxH);

    // Cancel current encoding
    void cancel();

    // State queries
    bool isComplete() const { return phase_ == Phase::Complete; }
    bool hasFailed() const { return phase_ == Phase::Failed; }
    bool isCancelled() const { return phase_ == Phase::Cancelled; }
    bool isRunning() const { return phase_ != Phase::Idle && phase_ != Phase::Complete && phase_ != Phase::Failed && phase_ != Phase::Cancelled; }
    float getProgress() const { return progress_; }

    // Get info
    int getCurrentFrame() const { return currentFrame_; }
    int getTotalFrames() const { return totalFrames_; }
    int getEncodedFrames() const;
    const string& getInputPath() const { return settings_.inputPath; }
    const string& getOutputPath() const { return settings_.outputPath; }

    // Video info (available after begin())
    int getVideoWidth() const { return source_ ? static_cast<int>(source_->getWidth()) : 0; }
    int getVideoHeight() const { return source_ ? static_cast<int>(source_->getHeight()) : 0; }
    float getVideoFps() const {
        if (!source_) return 30.0f;
        float duration = source_->getDuration();
        return (duration > 0 && totalFrames_ > 0) ? totalFrames_ / duration : 30.0f;
    }

    // Audio info (available after begin())
    bool hasAudio() const { return !audioData_.empty() && audioCodec_ != 0; }
    uint32_t getAudioCodec() const { return audioCodec_; }
    int getAudioSampleRate() const { return audioSampleRate_; }
    int getAudioChannels() const { return audioChannels_; }
    size_t getAudioDataSize() const { return audioData_.size(); }

    // Get current phase as string (for display)
    string getPhaseString() const;

    // Get source video texture for preview (only valid while encoding)
    const Texture& getSourceTexture() const { return source_->getTexture(); }
    bool hasSourceTexture() const { return source_ && source_->isLoaded(); }

private:
    enum class Phase {
        Idle,
        Encoding,
        Complete,
        Failed,
        Cancelled
    };

    Phase phase_ = Phase::Idle;
    Settings settings_;

    unique_ptr<VideoPlayerBase> source_;
    TcvEncoder encoder_;

    int currentFrame_ = 0;
    int totalFrames_ = 0;
    float progress_ = 0.0f;

    // Frame extraction state
    bool waitingForFrame_ = false;
    int waitCounter_ = 0;
    int retryCount_ = 0;

    // Audio data extracted from source
    vector<uint8_t> audioData_;
    uint32_t audioCodec_ = 0;
    int audioSampleRate_ = 0;
    int audioChannels_ = 0;

    static constexpr int kWaitTimeout = 100;
    static constexpr int MAX_RETRIES = 3;
    static constexpr float END_THRESHOLD = 0.98f;

    void encodeNextFrame();
    void finishEncoding();
};
