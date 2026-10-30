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

// The URL is data, never JavaScript source; the script size is constant.
EM_JS(int, tcVideoBeginLoad, (const char* pathPtr, size_t pathLength), {
    // Stop existing video if any
    if (window._trussc_player_video) {
        window._trussc_player_video.pause();
        window._trussc_player_video.remove();
    }

    if (window._trussc_player_object_url) {
        URL.revokeObjectURL(window._trussc_player_object_url);
        window._trussc_player_object_url = null;
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
    window._trussc_player_staging_bytes = 0;
    window._trussc_player_staging_peak = 0;

    // Create video element
    var video = document.createElement('video');
    video.setAttribute("playsinline", "true");
    video.crossOrigin = 'anonymous';
    // preload the first frame so update() can upload it BEFORE play()
    // (the web equivalent of the auto-poster: no black flash)
    video.preload = 'auto';
    video.style.display = 'none';
    document.body.appendChild(video);
    window._trussc_player_video = video;

    // Resolve path (data/xxx.mp4 -> xxx.mp4 or absolute URL)
    var videoPath = UTF8ToString(pathPtr, pathLength);
    if (videoPath.startsWith('data/')) {
        videoPath = videoPath.substring(5);
    }

    // Lookup failure alone means this is a URL. Once a file is
    // found, staging failures must remain structured load failures.
    var node = null;
    try { node = FS.lookupPath(videoPath).node; } catch (e) {}
    if (node) {
        try {
            var blob;
            if (node.contents instanceof Blob) {
                // WORKERFS already has a Blob: reuse it with no copy.
                blob = node.contents;
            } else if (node.contents && node.contents.subarray) {
                // MEMFS is app-packaged/app-written data. Blob snapshots
                // this view: one file-sized staging copy (formerly two).
                var view = node.contents.subarray(0, node.usedBytes);
                blob = new Blob([view], { type: 'video/mp4' });
            } else {
                var capacity = 67108864;
                var needed = FS.stat(videoPath).size;
                function rejectStaging(bytes) {
                    window._trussc_player_error = {message: 'Video file staging exceeds 64 MiB',
                        code: -863, store: 'staging', needed: bytes, capacity: capacity};
                }
                // Bound (security): other-filesystem input is at most 64 MiB, with chunks plus Blob charged as staging copies. Overflow rejects the whole load and is counted.
                if (needed > capacity) { rejectStaging(needed); return 1; }
                var chunks = [], total = 0, retained = 0;
                var stream = FS.open(videoPath, 'r');
                try {
                    var eof = false;
                    while (!eof) {
                        if (total === capacity) {
                            // Probe one byte using admitted storage. On overflow
                            // it is discarded with the rest of the whole load.
                            if (FS.read(stream, chunks[0], 0, 1) > 0) {
                                needed = total + 1;
                                // The opened handle supplies diagnostics only; its size never sizes a read.
                                try { needed = Math.max(needed, FS.fstat(stream.fd).size); } catch (e) {}
                                rejectStaging(needed); return 1;
                            }
                            break;
                        }
                        var chunk = new Uint8Array(Math.min(1048576, capacity - total));
                        var filled = 0;
                        while (filled < chunk.length) {
                            var count = FS.read(stream, chunk, filled, chunk.length - filled);
                            if (count === 0) { eof = true; break; }
                            filled += count;
                            total += count;
                        }
                        if (filled) {
                            chunks.push(chunk.subarray(0, filled));
                            retained += chunk.byteLength;
                        }
                        window._trussc_player_staging_peak = Math.max(
                            window._trussc_player_staging_peak, retained + (filled ? 0 : chunk.byteLength));
                        chunk = null;
                    }
                } finally { FS.close(stream); }
                // At most 64 MiB of chunk capacity plus 64 MiB of Blob.
                // No Blob or URL is published until the complete read passes.
                window._trussc_player_staging_peak = Math.max(
                    window._trussc_player_staging_peak, retained + total);
                blob = new Blob(chunks, { type: 'video/mp4' });
                chunks = null;
                window._trussc_player_staging_bytes = total;
            }
            videoPath = URL.createObjectURL(blob);
            window._trussc_player_object_url = videoPath;
        } catch (e) {
            window._trussc_player_error = {message: 'Video file staging failed: ' + String(e), code: -864};
            return 1;
        }
    }

    video.src = videoPath;

    video.onloadedmetadata = function() {
        if (window._trussc_player_video !== video) return;
        var w = video.videoWidth, h = video.videoHeight;
        if (w <= 0 || h <= 0) {
            window._trussc_player_error = {message: 'Invalid video frame dimensions', code: -863};
            video.pause(); return;
        }
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
        if (window._trussc_player_video !== video) return;
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
});

EM_JS(double, tcVideoStagingBytes, (), { return window._trussc_player_staging_bytes || 0; });
EM_JS(double, tcVideoStagingPeak, (), { return window._trussc_player_staging_peak || 0; });
EM_JS(double, tcVideoErrorNeeded, (), { return window._trussc_player_error.needed || 0; });
EM_JS(double, tcVideoErrorCapacity, (), { return window._trussc_player_error.capacity || 0; });
EM_JS(int, tcVideoErrorIsStaging, (), { return window._trussc_player_error.store === 'staging'; });

static void reportWebVideoStorageError(internal::VideoDecodeStatus& status) {
    status.reject(uint64_t(tcVideoErrorNeeded()), uint64_t(tcVideoErrorCapacity()),
        tcVideoErrorIsStaging() ? "Video file staging" : "Video decode storage");
    status.warn();
}

bool VideoPlayer::loadPlatform(const fs::path& path) {
    webLoadResult_ = LoadResult::success();
    decodeStatus_ = std::make_shared<internal::VideoDecodeStatus>();
    const double deadline = emscripten_get_now() + internal::webLoadTimeoutMSecs;
    std::string pathStr = internal::pathToUtf8(path);
    int result = tcVideoBeginLoad(pathStr.c_str(), pathStr.size());
    if (result <= 0) {
        logError("VideoPlayer") << "failed to load '" << pathStr << "' [Web]";
        return false;
    }

    // A successful synchronous load has a decoded first frame, including in setup().
    while (!emscripten_run_script_int(
        "!!window._trussc_player_error || (window._trussc_player_ready && "
        "window._trussc_player_video.readyState >= 2)")) {
        if (emscripten_get_now() >= deadline) {
            closePlatform();
            webLoadResult_ = internal::reportWebLoadTimeout(path, "VideoPlayer");
            return false;
        }
        emscripten_sleep(10);
    }
    if (emscripten_run_script_int("!!window._trussc_player_error")) {
        if (emscripten_run_script_int("window._trussc_player_error.code == -863")) {
            reportWebVideoStorageError(*decodeStatus_);
        }
        const std::string message = emscripten_run_script_string("window._trussc_player_error.message");
        webLoadResult_ = LoadResult::fail(LoadError::DecodeFailed, message);
        logError("VideoPlayer") << "failed to decode '" << pathStr << "' [Web]: " << message;
        closePlatform();
        return false;
    }
    width_ = emscripten_run_script_int("window._trussc_player_width");
    height_ = emscripten_run_script_int("window._trussc_player_height");
    size_t bytes = 0;
    if (!internal::VideoDecodeLimits::rgba(width_, height_, bytes)) {
        decodeStatus_->reject(bytes, 0);
        decodeStatus_->warn();
        closePlatform();
        return false;
    }

    // Allocate pixel buffer
    pixels_ = new (std::nothrow) unsigned char[bytes];
    if (!pixels_) { closePlatform(); return false; }
    std::memset(pixels_, 0, bytes);

    updatePlatform();
    logNotice("VideoPlayer") << "loaded '" << pathStr << "' [Web]";
    return true;
}

void VideoPlayer::closePlatform() {
    emscripten_run_script(R"JS(
        window._trussc_player_ready = false;
        window._trussc_player_playing = false;

        if (window._trussc_player_video) {
            var video = window._trussc_player_video;
            window._trussc_player_video = null;
            video.onloadedmetadata = video.onended = video.onerror = null;
            video.pause();
            video.removeAttribute('src');
            video.load(); // Abort any pending network request.
            video.remove();
        }
        if (window._trussc_player_object_url) {
            URL.revokeObjectURL(window._trussc_player_object_url);
            window._trussc_player_object_url = null;
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
        if (code == -863 && decodeStatus_) {
            reportWebVideoStorageError(*decodeStatus_);
        }
        emscripten_run_script("window._trussc_player_error = null;");
        reportPlaybackError(message, code);
        return;
    }
    // Update size if changed
    int newWidth = emscripten_run_script_int("(window._trussc_player_video ? window._trussc_player_video.videoWidth : 0)");
    int newHeight = emscripten_run_script_int("(window._trussc_player_video ? window._trussc_player_video.videoHeight : 0)");

    size_t bytes = 0;
    if (newWidth > 0 && newHeight > 0 && !internal::VideoDecodeLimits::rgba(newWidth, newHeight, bytes)) {
        decodeStatus_->reject(bytes, 0);
        decodeStatus_->warn();
        reportPlaybackError("Invalid video frame dimensions", -863);
        return;
    }
    if (newWidth > 0 && newHeight > 0 && (newWidth != width_ || newHeight != height_)) {
        auto sizeResult = internal::checkImageSize(newWidth, newHeight);
        if (!sizeResult) {
            reportPlaybackError(sizeResult.message, -863);
            return;
        }
        // Release the old canvas and texture before allocating the new size.
        emscripten_run_script("if (window._trussc_player_canvas) { window._trussc_player_canvas.width = 0; window._trussc_player_canvas.height = 0; }");
        texture_.clear();
        width_ = newWidth;
        height_ = newHeight;
        delete[] pixels_;
        pixels_ = nullptr;

        texture_.allocate(width_, height_, 4, TextureUsage::Stream);
        if (!texture_.isAllocated()) {
            reportPlaybackError("Video texture allocation failed", -863);
            return;
        }

        // Size the CPU mirror only after the replacement texture is available.
        pixels_ = new (std::nothrow) unsigned char[bytes];
        if (!pixels_) { reportPlaybackError("Video frame allocation failed", -863); return; }
        std::memset(pixels_, 0, bytes);

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

            var w = video.videoWidth;
            var h = video.videoHeight;
            if (w <= 0 || h <= 0) {
                window._trussc_player_error = {message: 'Invalid video frame dimensions', code: -863};
                video.pause(); return 0;
            }
            if (w != %d || h != %d) return 0;
            if (canvas.width != w || canvas.height != h) {
                canvas.width = w; canvas.height = h;
            }

            // Draw video to canvas
            ctx.drawImage(video, 0, 0, w, h);

            // Get pixel data
            var imageData = ctx.getImageData(0, 0, w, h);
            var data = imageData.data;
            if (data.length != w * h * 4) {
                window._trussc_player_error = {message: 'Invalid video frame layout', code: -863};
                video.pause(); return 0;
            }

            // Copy to WASM memory (RGBA)
            var outBuffer = %u;
            for (var i = 0; i < data.length; i++) {
                HEAPU8[outBuffer + i] = data[i];
            }

            window._trussc_player_frameNew = true;
            return 1;
        })();
    )JS", width_, height_, (unsigned int)(uintptr_t)pixels_);

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

bool VideoPlayer::extractFramePlatform(const fs::path&, Pixels&, float, float*, std::shared_ptr<internal::VideoDecodeStatus>) {
    return false;
}

bool VideoPlayer::extractKeyFramePlatform(const fs::path&, Pixels&, float, float*, std::shared_ptr<internal::VideoDecodeStatus>) {
    return false;
}

} // namespace trussc

#endif // __EMSCRIPTEN__
