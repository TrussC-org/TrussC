// =============================================================================
// tcMicInput Web implementation
// Microphone input using getUserMedia + Web Audio API
// =============================================================================

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include "tc/sound/tcSound.h"
#include <cstring>
#include <cstdio>

namespace trussc {

// ---------------------------------------------------------------------------
// MicInput Web implementation
// ---------------------------------------------------------------------------

MicInput::~MicInput() {
    stop();
}

bool MicInput::start(int sampleRate) {
    if (running_) {
        stop();
    }

    sampleRate_ = sampleRate;
    buffer_.resize(BUFFER_SIZE, 0.0f);
    writePos_ = 0;

    // JavaScript側でマイク初期化（非同期）
    char script[2048];
    snprintf(script, sizeof(script), R"JS(
        (function() {
            var sampleRate = %d;
            var bufferSize = %d;

            // 既存のマイクがあれば停止
            if (window._trussc_mic_stream) {
                window._trussc_mic_stream.getTracks().forEach(function(t) { t.stop(); });
            }

            // リングバッファ初期化
            window._trussc_mic_buffer = new Float32Array(bufferSize);
            window._trussc_mic_writePos = 0;
            window._trussc_mic_running = false;

            // getUserMediaでマイクアクセス（非同期）
            navigator.mediaDevices.getUserMedia({
                audio: {
                    sampleRate: sampleRate,
                    channelCount: 1,
                    echoCancellation: false,
                    noiseSuppression: false,
                    autoGainControl: false
                }
            }).then(function(stream) {
                window._trussc_mic_stream = stream;

                // Web Audio API セットアップ
                var audioCtx = new (window.AudioContext || window.webkitAudioContext)({
                    sampleRate: sampleRate
                });
                window._trussc_mic_ctx = audioCtx;

                var source = audioCtx.createMediaStreamSource(stream);

                // ScriptProcessorNode でサンプル取得
                var processor = audioCtx.createScriptProcessor(bufferSize, 1, 1);
                window._trussc_mic_processor = processor;

                processor.onaudioprocess = function(e) {
                    if (!window._trussc_mic_running) return;

                    var input = e.inputBuffer.getChannelData(0);
                    var buffer = window._trussc_mic_buffer;
                    var size = buffer.length;

                    for (var i = 0; i < input.length; i++) {
                        buffer[window._trussc_mic_writePos] = input[i];
                        window._trussc_mic_writePos = (window._trussc_mic_writePos + 1) % size;
                    }
                };

                source.connect(processor);
                processor.connect(audioCtx.destination);

                window._trussc_mic_running = true;
                console.log('[MicInput] Web: started (' + audioCtx.sampleRate + ' Hz)');

            }).catch(function(err) {
                console.error('[MicInput] Web: failed to start -', err.message);
                window._trussc_mic_running = false;
            });
        })();
    )JS", sampleRate, BUFFER_SIZE);

    emscripten_run_script(script);

    running_ = true;
    printf("MicInput: started (%d Hz, mono) [Web]\n", sampleRate);
    return true;
}

void MicInput::stop() {
    if (!running_) return;

    emscripten_run_script(R"JS(
        window._trussc_mic_running = false;

        if (window._trussc_mic_processor) {
            window._trussc_mic_processor.disconnect();
            window._trussc_mic_processor = null;
        }
        if (window._trussc_mic_ctx) {
            window._trussc_mic_ctx.close();
            window._trussc_mic_ctx = null;
        }
        if (window._trussc_mic_stream) {
            window._trussc_mic_stream.getTracks().forEach(function(t) { t.stop(); });
            window._trussc_mic_stream = null;
        }

        console.log('[MicInput] Web: stopped');
    )JS");

    running_ = false;
    printf("MicInput: stopped [Web]\n");
}

size_t MicInput::getBuffer(float* outBuffer, size_t numSamples) {
    if (!running_ || numSamples == 0) return 0;

    numSamples = std::min(numSamples, (size_t)BUFFER_SIZE);

    // グローバル変数経由でポインタを渡す
    char script[512];
    snprintf(script, sizeof(script), R"JS(
        (function() {
            if (!window._trussc_mic_running || !window._trussc_mic_buffer) {
                return 0;
            }

            var outBuffer = %u;
            var numSamples = %d;
            var buffer = window._trussc_mic_buffer;
            var size = buffer.length;
            var writePos = window._trussc_mic_writePos;

            numSamples = Math.min(numSamples, size);

            // リングバッファから最新サンプルを取得
            var readPos = (writePos + size - numSamples) %% size;

            for (var i = 0; i < numSamples; i++) {
                HEAPF32[(outBuffer >> 2) + i] = buffer[(readPos + i) %% size];
            }

            return numSamples;
        })();
    )JS", (unsigned int)(uintptr_t)outBuffer, (int)numSamples);

    int result = emscripten_run_script_int(script);
    return (size_t)result;
}

void MicInput::onAudioData(const float* input, size_t frameCount) {
    // Web版では使用しない（JSで直接処理）
    (void)input;
    (void)frameCount;
}

// ---------------------------------------------------------------------------
// Global instance
// ---------------------------------------------------------------------------
MicInput& getMicInput() {
    static MicInput instance;
    return instance;
}

} // namespace trussc

#endif // __EMSCRIPTEN__
