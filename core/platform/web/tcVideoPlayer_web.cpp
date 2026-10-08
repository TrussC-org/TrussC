// =============================================================================
// tcVideoPlayer Web implementation
// Video playback using HTML5 video element + Canvas API
// =============================================================================

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include "TrussC.h"
#include <cstring>
#include <cstdio>

namespace trussc {

// ---------------------------------------------------------------------------
// VideoPlayer Web implementation
// ---------------------------------------------------------------------------

bool VideoPlayer::loadPlatform(const fs::path& path) {
    std::string pathStr = internal::pathToUtf8(path);
    // Create video element in JavaScript
    char script[8192];
    snprintf(script, sizeof(script), R"JS(
        (function() {
            // Stop existing video if any
            if (window._trussc_player_video) {
                window._trussc_player_video.pause();
                window._trussc_player_video.remove();
            }

            // Initialize state
            window._trussc_player_error = null;
            window._trussc_player_ready = false;
            window._trussc_player_playing = false;
            window._trussc_player_frameNew = false;
            window._trussc_player_finished = false;
            window._trussc_player_width = 0;
            window._trussc_player_height = 0;
            window._trussc_player_duration = 0;

            // Create video element
            var video = document.createElement('video');
            video.setAttribute('playsinline', '');
            video.crossOrigin = 'anonymous';
            // preload the first frame so update() can upload it BEFORE play()
            // (the web equivalent of the auto-poster: no black flash)
            video.preload = 'auto';
            video.style.display = 'none';
            document.body.appendChild(video);
            window._trussc_player_video = video;

            // Resolve path (data/xxx.mp4 -> xxx.mp4 or absolute URL)
            var videoPath = '%s';
            if (videoPath.startsWith('data/')) {
                videoPath = videoPath.substring(5);
            }

            // Check WASM preloaded files
            var blob = null;
            try {
                var data = FS.readFile(videoPath);
                blob = new Blob([data], { type: 'video/mp4' });
                videoPath = URL.createObjectURL(blob);
                console.log('[VideoPlayer] Web: loaded from virtual FS');
            } catch(e) {
                // Not in filesystem, use as URL directly
                console.log('[VideoPlayer] Web: loading from URL -', videoPath);
            }

            video.src = videoPath;

            video.onloadedmetadata = function() {
                window._trussc_player_width = video.videoWidth;
                window._trussc_player_height = video.videoHeight;
                window._trussc_player_duration = video.duration;
                window._trussc_player_ready = true;

                // Create canvas
                var canvas = document.createElement('canvas');
                canvas.width = video.videoWidth;
                canvas.height = video.videoHeight;
                window._trussc_player_canvas = canvas;
                window._trussc_player_ctx = canvas.getContext('2d', { willReadFrequently: true });

                console.log('[VideoPlayer] Web: loaded (' + video.videoWidth + 'x' + video.videoHeight + ', ' + video.duration.toFixed(2) + 's)');
            };

            video.onended = function() {
                window._trussc_player_finished = true;
                if (video.loop) {
                    window._trussc_player_finished = false;
                }
            };

            video.onerror = function(e) {
                if (window._trussc_player_video !== video) return;
                window._trussc_player_error = {
                    message: video.error && video.error.message || 'Video playback failed',
                    code: video.error ? video.error.code : 0
                };
                video.pause();
                window._trussc_player_playing = false;
            };

            // Start loading
            video.load();

            return 1;
        })();
    )JS", pathStr.c_str());

    int result = emscripten_run_script_int(script);
    if (result <= 0) {
        logError("VideoPlayer") << "failed to load '" << pathStr << "' [Web]";
        return false;
    }

    // Set initial values (metadata loaded asynchronously)
    width_ = 640;
    height_ = 480;

    // Allocate pixel buffer
    pixels_ = new unsigned char[width_ * height_ * 4];
    std::memset(pixels_, 0, width_ * height_ * 4);

    logNotice("VideoPlayer") << "loading '" << pathStr << "' [Web]";
    return true;
}

void VideoPlayer::closePlatform() {
    emscripten_run_script(R"JS(
        window._trussc_player_ready = false;
        window._trussc_player_playing = false;

        if (window._trussc_player_video) {
            window._trussc_player_video.pause();
            window._trussc_player_video.remove();
            window._trussc_player_video = null;
        }
        window._trussc_player_canvas = null;
        window._trussc_player_ctx = null;

        console.log('[VideoPlayer] Web: closed');
    )JS");

    logVerbose("VideoPlayer") << "closed [Web]";
}

void VideoPlayer::playPlatform() {
    emscripten_run_script(R"JS(
        if (window._trussc_player_video && window._trussc_player_ready) {
            window._trussc_player_video.play();
            window._trussc_player_playing = true;
            window._trussc_player_finished = false;
        }
    )JS");
}

void VideoPlayer::stopPlatform() {
    emscripten_run_script(R"JS(
        if (window._trussc_player_video) {
            window._trussc_player_video.pause();
            window._trussc_player_video.currentTime = 0;
            window._trussc_player_playing = false;
            window._trussc_player_finished = false;
        }
    )JS");
}

void VideoPlayer::setPausedPlatform(bool paused) {
    if (paused) {
        emscripten_run_script("if (window._trussc_player_video) window._trussc_player_video.pause();");
    } else {
        emscripten_run_script("if (window._trussc_player_video) window._trussc_player_video.play();");
    }
}

void VideoPlayer::updatePlatform() {
    if (emscripten_run_script_int("!!window._trussc_player_error")) {
        const std::string message = emscripten_run_script_string("window._trussc_player_error.message");
        int code = emscripten_run_script_int("window._trussc_player_error.code");
        emscripten_run_script("window._trussc_player_error = null;");
        reportPlaybackError(message, code);
        return;
    }
    // Update size if changed
    int newWidth = emscripten_run_script_int("(window._trussc_player_width || 0)");
    int newHeight = emscripten_run_script_int("(window._trussc_player_height || 0)");

    if (newWidth > 0 && newHeight > 0 && (newWidth != width_ || newHeight != height_)) {
        width_ = newWidth;
        height_ = newHeight;

        // Reallocate pixel buffer
        delete[] pixels_;
        pixels_ = new unsigned char[width_ * height_ * 4];
        std::memset(pixels_, 0, width_ * height_ * 4);

        // Reallocate texture
        texture_.allocate(width_, height_, 4, TextureUsage::Stream);

        logVerbose("VideoPlayer") << "resized to " << width_ << "x" << height_ << " [Web]";
    }

    if (!pixels_ || width_ <= 0 || height_ <= 0) return;

    // Get pixel data from JavaScript
    char script[2048];
    snprintf(script, sizeof(script), R"JS(
        (function() {
            var video = window._trussc_player_video;
            if (!video || !window._trussc_player_ready) {
                return 0;
            }

            var canvas = window._trussc_player_canvas;
            var ctx = window._trussc_player_ctx;
            if (!canvas || !ctx) return 0;

            // readyState >= 2 (HAVE_CURRENT_DATA) means frame is available
            if (video.readyState < 2) return 0;

            var w = canvas.width;
            var h = canvas.height;

            // Draw video to canvas
            ctx.drawImage(video, 0, 0, w, h);

            // Get pixel data
            var imageData = ctx.getImageData(0, 0, w, h);
            var data = imageData.data;

            // Copy to WASM memory (RGBA)
            var outBuffer = %u;
            for (var i = 0; i < data.length; i++) {
                HEAPU8[outBuffer + i] = data[i];
            }

            window._trussc_player_frameNew = true;
            return 1;
        })();
    )JS", (unsigned int)(uintptr_t)pixels_);

    emscripten_run_script_int(script);
}

bool VideoPlayer::hasNewFramePlatform() const {
    int result = emscripten_run_script_int(R"JS(
        (function() {
            if (window._trussc_player_frameNew) {
                window._trussc_player_frameNew = false;
                return 1;
            }
            return 0;
        })();
    )JS");
    return result != 0;
}

bool VideoPlayer::isFinishedPlatform() const {
    int result = emscripten_run_script_int("(window._trussc_player_finished ? 1 : 0)");
    return result != 0;
}

float VideoPlayer::getPositionPlatform() const {
    // emscripten_run_script_double doesn't exist, multiply by 1000 and get as int
    int pos1000 = emscripten_run_script_int(R"JS(
        (function() {
            var video = window._trussc_player_video;
            if (!video || !window._trussc_player_ready || video.duration <= 0) return 0;
            return Math.floor((video.currentTime / video.duration) * 1000);
        })();
    )JS");
    return pos1000 / 1000.0f;
}

void VideoPlayer::setPositionPlatform(float pct) {
    char script[256];
    snprintf(script, sizeof(script), R"JS(
        (function() {
            var video = window._trussc_player_video;
            if (video && window._trussc_player_ready && video.duration > 0) {
                video.currentTime = %f * video.duration;
            }
        })();
    )JS", pct);
    emscripten_run_script(script);
}

float VideoPlayer::getDurationPlatform() const {
    // Multiply by 1000 and get as int
    int dur1000 = emscripten_run_script_int("(Math.floor((window._trussc_player_duration || 0) * 1000))");
    return dur1000 / 1000.0f;
}

void VideoPlayer::setVolumePlatform(float vol) {
    char script[128];
    snprintf(script, sizeof(script), "if (window._trussc_player_video) window._trussc_player_video.volume = %f;", vol);
    emscripten_run_script(script);
}

void VideoPlayer::setSpeedPlatform(float speed) {
    char script[128];
    snprintf(script, sizeof(script), "if (window._trussc_player_video) window._trussc_player_video.playbackRate = %f;", speed);
    emscripten_run_script(script);
}

void VideoPlayer::setLoopPlatform(bool loop) {
    emscripten_run_script(loop ?
        "if (window._trussc_player_video) window._trussc_player_video.loop = true;" :
        "if (window._trussc_player_video) window._trussc_player_video.loop = false;");
}

// HTMLVideoElement does not provide a reliable file frame rate.
// The shared API guard warns once; time-based seeking remains available.
float VideoPlayer::getFrameRatePlatform() const { return 0.0f; }
int VideoPlayer::getCurrentFramePlatform() const { return 0; }
int VideoPlayer::getTotalFramesPlatform() const { return 0; }
void VideoPlayer::setFramePlatform(int) {}
void VideoPlayer::nextFramePlatform() {}
void VideoPlayer::previousFramePlatform() {}

// ---------------------------------------------------------------------------
// Audio-related stubs (not yet implemented for Web)
// ---------------------------------------------------------------------------

bool VideoPlayer::hasAudioPlatform() const {
    // Could check video.audioTracks but not widely supported
    return false;
}

uint32_t VideoPlayer::getAudioCodecPlatform() const {
    logWarning("VideoPlayer") << "getAudioCodec() is not supported on Web platform";
    return 0;
}

std::vector<uint8_t> VideoPlayer::getAudioDataPlatform() const {
    logWarning("VideoPlayer") << "getAudioData() is not supported on Web platform";
    return {};
}

int VideoPlayer::getAudioSampleRatePlatform() const {
    logWarning("VideoPlayer") << "getAudioSampleRate() is not supported on Web platform";
    return 0;
}

int VideoPlayer::getAudioChannelsPlatform() const {
    logWarning("VideoPlayer") << "getAudioChannels() is not supported on Web platform";
    return 0;
}

// Hardware acceleration info
// HTML5 <video> delegates decoding to the browser, which uses the GPU
// for mainstream codecs whenever available. We report "browser" to
// indicate that the decode path is out of TrussC's direct control.
bool VideoPlayer::isUsingHwAccelPlatform() const {
    return platformHandle_ != nullptr;
}

std::string VideoPlayer::getHwAccelNamePlatform() const {
    return platformHandle_ ? "browser" : "none";
}

// =============================================================================
// Frame extraction - unsupported on web (browsers have no synchronous media
// decode). The auto-poster path is covered natively instead: the <video>
// element preloads its first frame and update() uploads it before play().
// =============================================================================

bool VideoPlayer::extractFramePlatform(const fs::path&, Pixels&, float, float*) {
    return false;
}

bool VideoPlayer::extractKeyFramePlatform(const fs::path&, Pixels&, float, float*) {
    return false;
}

} // namespace trussc

#endif // __EMSCRIPTEN__
