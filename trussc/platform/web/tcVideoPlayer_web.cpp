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

bool VideoPlayer::loadPlatform(const std::string& path) {
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
            window._trussc_player_ready = false;
            window._trussc_player_playing = false;
            window._trussc_player_frameNew = false;
            window._trussc_player_finished = false;
            window._trussc_player_width = 0;
            window._trussc_player_height = 0;
            window._trussc_player_duration = 0;
            window._trussc_player_frameRate = 30;

            // Create video element
            var video = document.createElement('video');
            video.setAttribute('playsinline', '');
            video.crossOrigin = 'anonymous';
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
                console.error('[VideoPlayer] Web: failed to load -', video.error ? video.error.message : 'unknown error');
                window._trussc_player_ready = false;
            };

            // Start loading
            video.load();

            return 1;
        })();
    )JS", path.c_str());

    int result = emscripten_run_script_int(script);
    if (result <= 0) {
        printf("VideoPlayer: failed to load [Web]\n");
        return false;
    }

    // Set initial values (metadata loaded asynchronously)
    width_ = 640;
    height_ = 480;

    // Allocate pixel buffer
    pixels_ = new unsigned char[width_ * height_ * 4];
    std::memset(pixels_, 0, width_ * height_ * 4);

    printf("VideoPlayer: loading '%s' [Web]\n", path.c_str());
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

    printf("VideoPlayer: closed [Web]\n");
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

        printf("VideoPlayer: resized to %dx%d [Web]\n", width_, height_);
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

int VideoPlayer::getCurrentFramePlatform() const {
    return emscripten_run_script_int(R"JS(
        (function() {
            var video = window._trussc_player_video;
            if (!video || !window._trussc_player_ready) return 0;
            var fps = window._trussc_player_frameRate || 30;
            return Math.floor(video.currentTime * fps);
        })();
    )JS");
}

int VideoPlayer::getTotalFramesPlatform() const {
    return emscripten_run_script_int(R"JS(
        (function() {
            var video = window._trussc_player_video;
            if (!video || !window._trussc_player_ready) return 0;
            var fps = window._trussc_player_frameRate || 30;
            return Math.floor(video.duration * fps);
        })();
    )JS");
}

void VideoPlayer::setFramePlatform(int frame) {
    char script[512];
    snprintf(script, sizeof(script),
        "if (window._trussc_player_video && window._trussc_player_ready) {"
        "var fps = window._trussc_player_frameRate || 30;"
        "window._trussc_player_video.currentTime = %d / fps;"
        "}", frame);
    emscripten_run_script(script);
}

void VideoPlayer::nextFramePlatform() {
    emscripten_run_script(R"JS(
        (function() {
            var video = window._trussc_player_video;
            if (video && window._trussc_player_ready) {
                var fps = window._trussc_player_frameRate || 30;
                video.currentTime = Math.min(video.duration, video.currentTime + 1/fps);
            }
        })();
    )JS");
}

void VideoPlayer::previousFramePlatform() {
    emscripten_run_script(R"JS(
        (function() {
            var video = window._trussc_player_video;
            if (video && window._trussc_player_ready) {
                var fps = window._trussc_player_frameRate || 30;
                video.currentTime = Math.max(0, video.currentTime - 1/fps);
            }
        })();
    )JS");
}

} // namespace trussc

#endif // __EMSCRIPTEN__
