#pragma once

#include <TrussC.h>
#include <tcxTcv.h>
#include <thread>

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

    // State queries
    bool isComplete() const { return phase_ == Phase::Complete; }
    bool hasFailed() const { return phase_ == Phase::Failed; }
    bool isRunning() const { return phase_ != Phase::Idle && phase_ != Phase::Complete && phase_ != Phase::Failed; }
    float getProgress() const { return progress_; }

    // Get info
    int getCurrentFrame() const { return currentFrame_; }
    int getTotalFrames() const { return totalFrames_; }
    int getEncodedFrames() const;
    const string& getInputPath() const { return settings_.inputPath; }
    const string& getOutputPath() const { return settings_.outputPath; }

    // Get current phase as string (for display)
    string getPhaseString() const;

private:
    enum class Phase {
        Idle,
        Encoding,
        Complete,
        Failed
    };

    Phase phase_ = Phase::Idle;
    Settings settings_;

    VideoPlayer source_;
    TcvEncoder encoder_;

    int currentFrame_ = 0;
    int totalFrames_ = 0;
    float progress_ = 0.0f;

    // Frame extraction state
    bool waitingForFrame_ = false;
    int waitCounter_ = 0;
    int retryCount_ = 0;

    static constexpr int WAIT_TIMEOUT = 100;
    static constexpr int MAX_RETRIES = 3;
    static constexpr float END_THRESHOLD = 0.98f;

    void encodeNextFrame();
    void finishEncoding();
};
