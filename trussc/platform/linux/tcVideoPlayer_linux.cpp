// =============================================================================
// tcVideoPlayer_linux.cpp - Linux VideoPlayer implementation using FFmpeg
// =============================================================================
// Uses libavcodec/libavformat for video decoding.
// Frames are converted to RGBA and uploaded to sokol_gfx texture.
// =============================================================================

#ifdef __linux__

#include "TrussC.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}

#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>

using namespace trussc;

// =============================================================================
// TCVideoPlayerImpl - Linux implementation using FFmpeg
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

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

private:
    void decodeThread();
    bool decodeNextFrame();
    void seekToTime(double seconds);

    // FFmpeg context
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFrame* frameRGBA_ = nullptr;
    AVPacket* packet_ = nullptr;

    int videoStreamIndex_ = -1;

    // Video properties
    int width_ = 0;
    int height_ = 0;
    double duration_ = 0.0;
    double frameRate_ = 30.0;
    AVRational timeBase_ = {1, 1};

    // Playback state
    std::atomic<bool> isLoaded_{false};
    std::atomic<bool> isPlaying_{false};
    std::atomic<bool> isPaused_{false};
    std::atomic<bool> hasNewFrame_{false};
    std::atomic<bool> isFinished_{false};
    std::atomic<bool> isLoop_{false};
    std::atomic<bool> shouldStop_{false};
    std::atomic<bool> seekRequested_{false};
    std::atomic<double> seekTarget_{0.0};

    float volume_ = 1.0f;
    float speed_ = 1.0f;

    // Threading
    std::thread decodeThread_;
    std::mutex mutex_;
    std::condition_variable cv_;

    // Frame queue
    struct FrameData {
        std::vector<uint8_t> pixels;
        double pts;
    };
    std::queue<FrameData> frameQueue_;
    static constexpr size_t MAX_QUEUE_SIZE = 4;

    // Timing
    double currentPts_ = 0.0;
    double playbackStartTime_ = 0.0;
    double pausedTime_ = 0.0;

    // Pixel buffer
    uint8_t* rgbaBuffer_ = nullptr;
    int rgbaBufferSize_ = 0;
};

// =============================================================================
// Implementation
// =============================================================================

bool TCVideoPlayerImpl::load(const std::string& path, VideoPlayer* player) {
    // Open file
    if (avformat_open_input(&formatCtx_, path.c_str(), nullptr, nullptr) < 0) {
        logError("VideoPlayer") << "Failed to open file: " << path;
        return false;
    }

    // Get stream info
    if (avformat_find_stream_info(formatCtx_, nullptr) < 0) {
        logError("VideoPlayer") << "Failed to find stream info";
        avformat_close_input(&formatCtx_);
        return false;
    }

    // Find video stream
    for (unsigned int i = 0; i < formatCtx_->nb_streams; i++) {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex_ = i;
            break;
        }
    }

    if (videoStreamIndex_ < 0) {
        logError("VideoPlayer") << "No video stream found";
        avformat_close_input(&formatCtx_);
        return false;
    }

    AVStream* videoStream = formatCtx_->streams[videoStreamIndex_];
    AVCodecParameters* codecPar = videoStream->codecpar;

    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        logError("VideoPlayer") << "Codec not found";
        avformat_close_input(&formatCtx_);
        return false;
    }

    // Create codec context
    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        logError("VideoPlayer") << "Failed to allocate codec context";
        avformat_close_input(&formatCtx_);
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, codecPar) < 0) {
        logError("VideoPlayer") << "Failed to copy codec parameters";
        avcodec_free_context(&codecCtx_);
        avformat_close_input(&formatCtx_);
        return false;
    }

    // Open codec
    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        logError("VideoPlayer") << "Failed to open codec";
        avcodec_free_context(&codecCtx_);
        avformat_close_input(&formatCtx_);
        return false;
    }

    // Get video properties
    width_ = codecCtx_->width;
    height_ = codecCtx_->height;
    timeBase_ = videoStream->time_base;

    // Calculate frame rate
    if (videoStream->avg_frame_rate.num > 0 && videoStream->avg_frame_rate.den > 0) {
        frameRate_ = av_q2d(videoStream->avg_frame_rate);
    } else if (videoStream->r_frame_rate.num > 0 && videoStream->r_frame_rate.den > 0) {
        frameRate_ = av_q2d(videoStream->r_frame_rate);
    }

    // Calculate duration
    if (formatCtx_->duration > 0) {
        duration_ = formatCtx_->duration / (double)AV_TIME_BASE;
    } else if (videoStream->duration > 0) {
        duration_ = videoStream->duration * av_q2d(timeBase_);
    }

    logNotice("VideoPlayer") << "Video: " << width_ << "x" << height_
                               << " @ " << frameRate_ << " fps, "
                               << duration_ << " sec";

    // Create scaler context (convert to RGBA)
    swsCtx_ = sws_getContext(
        width_, height_, codecCtx_->pix_fmt,
        width_, height_, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsCtx_) {
        logError("VideoPlayer") << "Failed to create scaler context";
        avcodec_free_context(&codecCtx_);
        avformat_close_input(&formatCtx_);
        return false;
    }

    // Allocate frames
    frame_ = av_frame_alloc();
    frameRGBA_ = av_frame_alloc();
    packet_ = av_packet_alloc();

    if (!frame_ || !frameRGBA_ || !packet_) {
        logError("VideoPlayer") << "Failed to allocate frames";
        close();
        return false;
    }

    // Allocate RGBA buffer
    rgbaBufferSize_ = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width_, height_, 1);
    rgbaBuffer_ = (uint8_t*)av_malloc(rgbaBufferSize_);

    av_image_fill_arrays(
        frameRGBA_->data, frameRGBA_->linesize,
        rgbaBuffer_, AV_PIX_FMT_RGBA,
        width_, height_, 1
    );

    isLoaded_ = true;
    return true;
}

void TCVideoPlayerImpl::close() {
    // Stop decode thread
    shouldStop_ = true;
    cv_.notify_all();

    if (decodeThread_.joinable()) {
        decodeThread_.join();
    }

    // Clear frame queue
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!frameQueue_.empty()) {
            frameQueue_.pop();
        }
    }

    // Free FFmpeg resources
    if (rgbaBuffer_) {
        av_free(rgbaBuffer_);
        rgbaBuffer_ = nullptr;
    }

    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }

    if (frameRGBA_) {
        av_frame_free(&frameRGBA_);
        frameRGBA_ = nullptr;
    }

    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }

    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }

    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }

    isLoaded_ = false;
    isPlaying_ = false;
    isPaused_ = false;
    hasNewFrame_ = false;
    isFinished_ = false;
    width_ = 0;
    height_ = 0;
}

void TCVideoPlayerImpl::play() {
    if (!isLoaded_) return;

    // Reset state
    isFinished_ = false;
    shouldStop_ = false;

    // Seek to beginning if finished
    if (currentPts_ >= duration_ - 0.1) {
        seekToTime(0.0);
    }

    // Start decode thread if not running
    if (!decodeThread_.joinable()) {
        decodeThread_ = std::thread(&TCVideoPlayerImpl::decodeThread, this);
    }

    playbackStartTime_ = av_gettime_relative() / 1000000.0 - currentPts_;
    isPlaying_ = true;
    isPaused_ = false;
    cv_.notify_all();
}

void TCVideoPlayerImpl::stop() {
    isPlaying_ = false;
    isPaused_ = false;

    // Seek to beginning
    seekToTime(0.0);
    currentPts_ = 0.0;

    // Clear frame queue
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!frameQueue_.empty()) {
            frameQueue_.pop();
        }
    }
}

void TCVideoPlayerImpl::setPaused(bool paused) {
    if (paused && !isPaused_) {
        // Pause: record current time
        pausedTime_ = av_gettime_relative() / 1000000.0;
        isPaused_ = true;
    } else if (!paused && isPaused_) {
        // Resume: adjust start time
        double pauseDuration = av_gettime_relative() / 1000000.0 - pausedTime_;
        playbackStartTime_ += pauseDuration;
        isPaused_ = false;
        cv_.notify_all();
    }
}

void TCVideoPlayerImpl::update(VideoPlayer* player) {
    hasNewFrame_ = false;

    if (!isLoaded_ || !isPlaying_ || isPaused_) return;

    // Calculate target PTS based on elapsed time
    double elapsed = av_gettime_relative() / 1000000.0 - playbackStartTime_;
    double targetPts = elapsed * speed_;

    // Get frame from queue if available and PTS is right
    std::lock_guard<std::mutex> lock(mutex_);

    while (!frameQueue_.empty()) {
        FrameData& front = frameQueue_.front();

        if (front.pts <= targetPts) {
            // Copy pixels to VideoPlayer's buffer
            if (player) {
                unsigned char* playerPixels = player->getPixels();
                if (playerPixels && front.pixels.size() == (size_t)(width_ * height_ * 4)) {
                    memcpy(playerPixels, front.pixels.data(), front.pixels.size());
                    hasNewFrame_ = true;
                    currentPts_ = front.pts;
                }
            }
            frameQueue_.pop();
        } else {
            // Frame is in the future, wait
            break;
        }
    }

    // Check if finished
    if (frameQueue_.empty() && isFinished_) {
        if (isLoop_) {
            seekToTime(0.0);
            playbackStartTime_ = av_gettime_relative() / 1000000.0;
            isFinished_ = false;
            cv_.notify_all();
        } else {
            isPlaying_ = false;
        }
    }

    cv_.notify_all();
}

void TCVideoPlayerImpl::decodeThread() {
    while (!shouldStop_) {
        // Wait if paused or queue is full
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return shouldStop_ ||
                       (isPlaying_ && !isPaused_ && frameQueue_.size() < MAX_QUEUE_SIZE) ||
                       seekRequested_;
            });
        }

        if (shouldStop_) break;

        // Handle seek request
        if (seekRequested_) {
            double target = seekTarget_;
            seekRequested_ = false;

            int64_t timestamp = (int64_t)(target / av_q2d(timeBase_));
            av_seek_frame(formatCtx_, videoStreamIndex_, timestamp, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codecCtx_);

            // Clear queue
            {
                std::lock_guard<std::mutex> lock(mutex_);
                while (!frameQueue_.empty()) {
                    frameQueue_.pop();
                }
            }

            currentPts_ = target;
            continue;
        }

        // Decode next frame
        if (!decodeNextFrame()) {
            isFinished_ = true;
        }
    }
}

bool TCVideoPlayerImpl::decodeNextFrame() {
    while (true) {
        int ret = av_read_frame(formatCtx_, packet_);
        if (ret < 0) {
            // End of file or error
            return false;
        }

        if (packet_->stream_index != videoStreamIndex_) {
            av_packet_unref(packet_);
            continue;
        }

        ret = avcodec_send_packet(codecCtx_, packet_);
        av_packet_unref(packet_);

        if (ret < 0) {
            continue;
        }

        ret = avcodec_receive_frame(codecCtx_, frame_);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        }
        if (ret < 0) {
            return false;
        }

        // Convert to RGBA
        sws_scale(
            swsCtx_,
            frame_->data, frame_->linesize,
            0, height_,
            frameRGBA_->data, frameRGBA_->linesize
        );

        // Calculate PTS
        double pts = 0.0;
        if (frame_->pts != AV_NOPTS_VALUE) {
            pts = frame_->pts * av_q2d(timeBase_);
        }

        // Add to queue
        {
            std::lock_guard<std::mutex> lock(mutex_);
            FrameData data;
            data.pixels.resize(width_ * height_ * 4);
            memcpy(data.pixels.data(), rgbaBuffer_, data.pixels.size());
            data.pts = pts;
            frameQueue_.push(std::move(data));
        }

        av_frame_unref(frame_);
        return true;
    }
}

void TCVideoPlayerImpl::seekToTime(double seconds) {
    seekTarget_ = seconds;
    seekRequested_ = true;
    cv_.notify_all();
}

float TCVideoPlayerImpl::getPosition() const {
    if (duration_ <= 0) return 0.0f;
    return static_cast<float>(currentPts_ / duration_);
}

void TCVideoPlayerImpl::setPosition(float pct) {
    double targetTime = pct * duration_;
    seekToTime(targetTime);
    playbackStartTime_ = av_gettime_relative() / 1000000.0 - targetTime;
}

float TCVideoPlayerImpl::getDuration() const {
    return static_cast<float>(duration_);
}

void TCVideoPlayerImpl::setVolume(float vol) {
    volume_ = vol;
    // Audio not implemented yet
}

void TCVideoPlayerImpl::setSpeed(float speed) {
    speed_ = speed;
    // Adjust playback start time to maintain position
    double currentTime = av_gettime_relative() / 1000000.0;
    playbackStartTime_ = currentTime - currentPts_ / speed_;
}

void TCVideoPlayerImpl::setLoop(bool loop) {
    isLoop_ = loop;
}

int TCVideoPlayerImpl::getCurrentFrame() const {
    if (frameRate_ <= 0) return 0;
    return static_cast<int>(currentPts_ * frameRate_);
}

int TCVideoPlayerImpl::getTotalFrames() const {
    if (frameRate_ <= 0) return 0;
    return static_cast<int>(duration_ * frameRate_);
}

void TCVideoPlayerImpl::setFrame(int frame) {
    if (frameRate_ > 0) {
        double time = frame / frameRate_;
        setPosition(static_cast<float>(time / duration_));
    }
}

void TCVideoPlayerImpl::nextFrame() {
    if (frameRate_ > 0) {
        double frameTime = 1.0 / frameRate_;
        seekToTime(currentPts_ + frameTime);
    }
}

void TCVideoPlayerImpl::previousFrame() {
    if (frameRate_ > 0) {
        double frameTime = 1.0 / frameRate_;
        double newTime = currentPts_ - frameTime;
        if (newTime < 0) newTime = 0;
        seekToTime(newTime);
    }
}

// =============================================================================
// VideoPlayer platform methods (Linux implementation)
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

} // namespace trussc

#endif // __linux__
