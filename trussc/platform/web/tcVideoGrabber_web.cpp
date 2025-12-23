// =============================================================================
// tcVideoGrabber Web implementation
// Webcam input using getUserMedia + Canvas API
// =============================================================================

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include "TrussC.h"
#include <cstring>
#include <cstdio>

namespace trussc {

// ---------------------------------------------------------------------------
// VideoGrabber Web implementation
// ---------------------------------------------------------------------------

std::vector<VideoDeviceInfo> VideoGrabber::listDevicesPlatform() {
    // Web版ではデバイスリスト取得は省略（非同期のため）
    // デフォルトカメラを使用
    std::vector<VideoDeviceInfo> devices;
    VideoDeviceInfo info;
    info.deviceId = 0;
    info.deviceName = "Default Camera";
    info.uniqueId = "default";
    devices.push_back(info);
    return devices;
}

bool VideoGrabber::setupPlatform() {
    // JavaScript側でカメラ初期化（非同期）
    char script[4096];
    snprintf(script, sizeof(script), R"JS(
        (function() {
            var width = %d;
            var height = %d;

            // 既存のカメラがあれば停止
            if (window._trussc_video_stream) {
                window._trussc_video_stream.getTracks().forEach(function(t) { t.stop(); });
            }

            // 状態初期化
            window._trussc_video_running = false;
            window._trussc_video_ready = false;
            window._trussc_video_width = width;
            window._trussc_video_height = height;
            window._trussc_video_actualWidth = 0;
            window._trussc_video_actualHeight = 0;
            window._trussc_video_needsResize = false;

            // video要素を作成
            var video = document.createElement('video');
            video.setAttribute('playsinline', '');
            video.setAttribute('autoplay', '');
            video.muted = true;
            video.style.display = 'none';
            document.body.appendChild(video);
            window._trussc_video_element = video;

            // canvas要素を作成（ピクセル取得用）
            var canvas = document.createElement('canvas');
            canvas.width = width;
            canvas.height = height;
            window._trussc_video_canvas = canvas;
            window._trussc_video_ctx = canvas.getContext('2d', { willReadFrequently: true });

            // getUserMediaでカメラアクセス
            navigator.mediaDevices.getUserMedia({
                video: {
                    width: { ideal: width },
                    height: { ideal: height }
                }
            }).then(function(stream) {
                window._trussc_video_stream = stream;
                video.srcObject = stream;

                video.onloadedmetadata = function() {
                    video.play();

                    // 実際の解像度を取得
                    var actualW = video.videoWidth;
                    var actualH = video.videoHeight;
                    window._trussc_video_actualWidth = actualW;
                    window._trussc_video_actualHeight = actualH;

                    // サイズが違う場合はリサイズフラグを立てる
                    if (actualW != width || actualH != height) {
                        canvas.width = actualW;
                        canvas.height = actualH;
                        window._trussc_video_needsResize = true;
                        console.log('[VideoGrabber] Web: actual size ' + actualW + 'x' + actualH + ' (requested ' + width + 'x' + height + ')');
                    }

                    window._trussc_video_ready = true;
                    window._trussc_video_running = true;
                    console.log('[VideoGrabber] Web: started (' + actualW + 'x' + actualH + ')');
                };

            }).catch(function(err) {
                console.error('[VideoGrabber] Web: failed to start -', err.message);
                window._trussc_video_running = false;
            });
        })();
    )JS", requestedWidth_, requestedHeight_);

    emscripten_run_script(script);

    // 初期サイズを設定（後でリサイズされる可能性あり）
    width_ = requestedWidth_;
    height_ = requestedHeight_;
    deviceName_ = "Web Camera";

    printf("VideoGrabber: setup requested (%dx%d) [Web]\n", width_, height_);
    return true;
}

void VideoGrabber::closePlatform() {
    emscripten_run_script(R"JS(
        window._trussc_video_running = false;
        window._trussc_video_ready = false;

        if (window._trussc_video_stream) {
            window._trussc_video_stream.getTracks().forEach(function(t) { t.stop(); });
            window._trussc_video_stream = null;
        }
        if (window._trussc_video_element) {
            window._trussc_video_element.remove();
            window._trussc_video_element = null;
        }
        window._trussc_video_canvas = null;
        window._trussc_video_ctx = null;

        console.log('[VideoGrabber] Web: stopped');
    )JS");

    printf("VideoGrabber: stopped [Web]\n");
}

void VideoGrabber::updatePlatform() {
    if (!pixels_) return;

    // JavaScriptからピクセルデータを取得
    char script[1024];
    snprintf(script, sizeof(script), R"JS(
        (function() {
            if (!window._trussc_video_running || !window._trussc_video_ready) {
                return 0;
            }

            var video = window._trussc_video_element;
            var canvas = window._trussc_video_canvas;
            var ctx = window._trussc_video_ctx;

            if (!video || !canvas || !ctx) return 0;

            var w = canvas.width;
            var h = canvas.height;

            // videoをcanvasに描画
            ctx.drawImage(video, 0, 0, w, h);

            // ピクセルデータを取得
            var imageData = ctx.getImageData(0, 0, w, h);
            var data = imageData.data;

            // WASMメモリにコピー（RGBA）
            var outBuffer = %u;
            for (var i = 0; i < data.length; i++) {
                HEAPU8[outBuffer + i] = data[i];
            }

            return 1;
        })();
    )JS", (unsigned int)(uintptr_t)pixels_);

    int result = emscripten_run_script_int(script);
    if (result > 0) {
        pixelsDirty_.store(true);
    }
}

void VideoGrabber::updateDelegatePixels() {
    // Web版では特に何もしない（pixels_は直接使用）
}

bool VideoGrabber::checkResizeNeeded() {
    int result = emscripten_run_script_int(R"JS(
        (window._trussc_video_needsResize ? 1 : 0)
    )JS");
    return result != 0;
}

void VideoGrabber::getNewSize(int& width, int& height) {
    width = emscripten_run_script_int("(window._trussc_video_actualWidth || 0)");
    height = emscripten_run_script_int("(window._trussc_video_actualHeight || 0)");
}

void VideoGrabber::clearResizeFlag() {
    emscripten_run_script("window._trussc_video_needsResize = false;");
}

bool VideoGrabber::checkCameraPermission() {
    // Web版では非同期のため、常にtrueを返す
    // 実際の許可はgetUserMediaで処理
    return true;
}

void VideoGrabber::requestCameraPermission() {
    // Web版ではgetUserMediaで自動的に許可ダイアログが出る
}

} // namespace trussc

#endif // __EMSCRIPTEN__
