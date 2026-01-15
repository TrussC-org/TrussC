#include "EncodingSession.h"

bool EncodingSession::begin(const Settings& settings) {
    settings_ = settings;

    // Load source video
    if (!source_.load(settings.inputPath)) {
        logError("EncodingSession") << "Failed to load video: " << settings.inputPath;
        phase_ = Phase::Failed;
        return false;
    }

    totalFrames_ = source_.getTotalFrames();
    if (totalFrames_ == 0) {
        logError("EncodingSession") << "Video has no frames";
        phase_ = Phase::Failed;
        return false;
    }

    // Calculate FPS
    float duration = source_.getDuration();
    float fps = (duration > 0) ? totalFrames_ / duration : 30.0f;

    // Configure encoder
    encoder_.setQuality(settings.quality);
    if (settings.partitions >= 0) encoder_.setPartitions(settings.partitions);
    if (settings.uber >= 0) encoder_.setUberLevel(settings.uber);
    encoder_.setThreadCount(settings.jobs);
    encoder_.setForceAllIFrames(settings.forceAllIFrames);
    encoder_.setEnableSkip(settings.enableSkip);
    encoder_.setEnableSolid(settings.enableSolid);

    // Start encoder
    if (!encoder_.begin(settings.outputPath,
                        static_cast<int>(source_.getWidth()),
                        static_cast<int>(source_.getHeight()),
                        fps)) {
        logError("EncodingSession") << "Failed to start encoder";
        phase_ = Phase::Failed;
        return false;
    }

    const char* qualityNames[] = {"fast", "balanced", "high"};
    logNotice("EncodingSession") << "Starting encode: " << settings.inputPath;
    logNotice("EncodingSession") << "Output: " << settings.outputPath;
    logNotice("EncodingSession") << "Size: " << source_.getWidth() << "x" << source_.getHeight();
    logNotice("EncodingSession") << "Frames: " << totalFrames_ << " @ " << fps << " fps";
    logNotice("EncodingSession") << "Quality: " << qualityNames[settings.quality];

    currentFrame_ = 0;
    progress_ = 0.0f;
    waitingForFrame_ = false;
    waitCounter_ = 0;
    retryCount_ = 0;

    source_.setFrame(0);
    phase_ = Phase::Encoding;

    return true;
}

void EncodingSession::update() {
    if (phase_ == Phase::Encoding) {
        encodeNextFrame();
    }
}

void EncodingSession::encodeNextFrame() {
    if (currentFrame_ >= totalFrames_) {
        finishEncoding();
        return;
    }

    // Request frame if not already waiting
    if (!waitingForFrame_) {
        if (currentFrame_ == 0) {
            source_.setFrame(0);
        } else {
            source_.nextFrame();
        }
        waitingForFrame_ = true;
        waitCounter_ = 0;
    } else {
        waitCounter_++;

        if (waitCounter_ > WAIT_TIMEOUT) {
            retryCount_++;
            if (retryCount_ > MAX_RETRIES) {
                // Check if we're near end (metadata frame count can be inaccurate)
                float frameProgress = static_cast<float>(currentFrame_) / totalFrames_;
                if (frameProgress > END_THRESHOLD) {
                    logNotice("EncodingSession") << "Reached end of video at frame " << currentFrame_
                                                  << " (metadata reported " << totalFrames_ << " frames)";
                } else {
                    logWarning("EncodingSession") << "Failed to decode frame " << currentFrame_
                                                  << " after retries. Finishing at "
                                                  << static_cast<int>(frameProgress * 100) << "%";
                }
                finishEncoding();
                return;
            }
            logNotice("EncodingSession") << "Waiting for frame " << currentFrame_
                                          << "... (attempt " << retryCount_ << ")";
            source_.setFrame(currentFrame_);
            waitCounter_ = 0;
            return;
        }
    }

    source_.update();

    if (!source_.isFrameNew()) {
        return;
    }

    // Got the frame
    waitingForFrame_ = false;
    retryCount_ = 0;

    const unsigned char* pixels = source_.getPixels();
    if (pixels) {
        encoder_.addFrame(pixels);
    }

    currentFrame_++;
    progress_ = static_cast<float>(currentFrame_) / totalFrames_;

    // Log progress periodically
    if (currentFrame_ % 100 == 0 || currentFrame_ == totalFrames_) {
        logNotice("EncodingSession") << "Frame " << currentFrame_ << " / " << totalFrames_
                                      << " (" << static_cast<int>(progress_ * 100) << "%)";
    }
}

void EncodingSession::finishEncoding() {
    encoder_.end();
    source_.close();

    logNotice("EncodingSession") << "Encoding complete: " << encoder_.getFrameCount() << " frames";

    phase_ = Phase::Complete;
}

void EncodingSession::draw(float x, float y, float maxW, float maxH) {
    if (phase_ == Phase::Idle) return;

    // Calculate preview size maintaining aspect ratio
    float srcW = source_.getWidth();
    float srcH = source_.getHeight();
    if (srcW == 0 || srcH == 0) return;

    float scale = std::min(maxW / srcW, maxH / srcH);
    float previewW = srcW * scale;
    float previewH = srcH * scale;

    // Draw video preview
    setColor(1.0f);
    source_.draw(x, y, previewW, previewH);

    // Progress bar
    float barY = y + previewH + 10;
    float barH = 16;

    setColor(0.3f);
    drawRect(x, barY, previewW, barH);

    setColor(0.2f, 0.8f, 0.4f);
    drawRect(x, barY, previewW * progress_, barH);

    // Progress text
    setColor(1.0f);
    string text = getPhaseString() + ": " + to_string(currentFrame_) + "/" + to_string(totalFrames_);
    drawBitmapString(text, x, barY + barH + 15);
}

int EncodingSession::getEncodedFrames() const {
    return encoder_.getFrameCount();
}

string EncodingSession::getPhaseString() const {
    switch (phase_) {
        case Phase::Idle: return "Idle";
        case Phase::Encoding: return "Encoding";
        case Phase::Complete: return "Complete";
        case Phase::Failed: return "Failed";
        default: return "Unknown";
    }
}
