// =============================================================================
// Android VideoPlayer implementation (stub)
// =============================================================================
// TODO: Implement using Android MediaCodec NDK API
// =============================================================================

#ifdef __ANDROID__

#include "TrussC.h"

#pragma message("STUB: tcVideoPlayer_android.cpp — needs MediaCodec NDK implementation")

namespace trussc {

bool VideoPlayer::loadPlatform(const std::string& path) {
    logWarning("VideoPlayer") << "Not yet implemented on Android";
    return false;
}

void VideoPlayer::closePlatform() {}
void VideoPlayer::playPlatform() {}
void VideoPlayer::stopPlatform() {}
void VideoPlayer::setPausedPlatform(bool paused) { (void)paused; }
void VideoPlayer::updatePlatform() {}
bool VideoPlayer::hasNewFramePlatform() const { return false; }
bool VideoPlayer::isFinishedPlatform() const { return false; }
float VideoPlayer::getPositionPlatform() const { return 0.0f; }
void VideoPlayer::setPositionPlatform(float pct) { (void)pct; }
float VideoPlayer::getDurationPlatform() const { return 0.0f; }
void VideoPlayer::setVolumePlatform(float vol) { (void)vol; }
void VideoPlayer::setSpeedPlatform(float speed) { (void)speed; }
void VideoPlayer::setLoopPlatform(bool loop) { (void)loop; }
int VideoPlayer::getCurrentFramePlatform() const { return 0; }
int VideoPlayer::getTotalFramesPlatform() const { return 0; }
void VideoPlayer::setFramePlatform(int frame) { (void)frame; }
void VideoPlayer::nextFramePlatform() {}
void VideoPlayer::previousFramePlatform() {}

bool VideoPlayer::hasAudioPlatform() const { return false; }
uint32_t VideoPlayer::getAudioCodecPlatform() const { return 0; }
std::vector<uint8_t> VideoPlayer::getAudioDataPlatform() const { return {}; }
int VideoPlayer::getAudioSampleRatePlatform() const { return 0; }
int VideoPlayer::getAudioChannelsPlatform() const { return 0; }

// Hardware acceleration info — Android VideoPlayer is currently a stub.
bool VideoPlayer::isUsingHwAccelPlatform() const { return false; }
std::string VideoPlayer::getHwAccelNamePlatform() const { return "none"; }

bool VideoPlayer::extractFramePlatform(const std::string& path, Pixels& outPixels,
                                       float timeSec, float* outDuration) {
    (void)path; (void)outPixels; (void)timeSec; (void)outDuration;
    return false;
}

} // namespace trussc

#endif // __ANDROID__
