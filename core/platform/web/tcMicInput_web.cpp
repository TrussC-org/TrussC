// =============================================================================
// tcMicInput Web implementation — per-instance getUserMedia / Web Audio state
// =============================================================================

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include "tc/sound/tcSound.h"
#include "tc/utils/tcLog.h"

namespace trussc {

namespace {
// Keys are never reused: a late permission result still owns its old state.
unsigned int nextMicInstanceId = 0;
}

MicInput::~MicInput() { stop(); }

bool MicInput::start(int sampleRate) {
    stop();
    sampleRate_ = sampleRate;
    webInstanceId_ = ++nextMicInstanceId;
    EM_ASM({
        var states = Module.trusscMicStates || (Module.trusscMicStates = new Map());
        var state = ({ stopped: false, running: false,
                      buffer: new Float32Array($2), writePos: 0 });
        states.set($0, state);
        navigator.mediaDevices.getUserMedia({ audio: {
            sampleRate: $1, channelCount: 1, echoCancellation: false,
            noiseSuppression: false, autoGainControl: false
        }}).then(function(stream) {
            // stop() may have run while permission was pending. The promise
            // owns this generation even after it is removed from the map.
            if (state.stopped) {
                stream.getTracks().forEach(function(track) { track.stop(); });
                return;
            }
            state.stream = stream;
            var ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: $1 });
            state.ctx = ctx;
            state.source = ctx.createMediaStreamSource(stream);
            state.processor = ctx.createScriptProcessor($2, 1, 1);
            state.processor.onaudioprocess = function(e) {
                if (state.stopped || !state.running) return;
                var input = e.inputBuffer.getChannelData(0);
                for (var i = 0; i < input.length; ++i) {
                    state.buffer[state.writePos] = input[i];
                    state.writePos = (state.writePos + 1) % state.buffer.length;
                }
            };
            state.source.connect(state.processor);
            state.processor.connect(ctx.destination);
            state.running = true;
        }).catch(function(err) {
            if (state.stopped) return;
            state.running = false;
            console.error('[MicInput] Web: failed to start -', err.message);
        });
    }, webInstanceId_, sampleRate, BUFFER_SIZE);
    running_ = true;
    logNotice("MicInput") << "started (" << sampleRate << " Hz, mono) [Web]";
    return true;
}

void MicInput::stop() {
    if (!running_) return;
    EM_ASM({
        var states = Module.trusscMicStates;
        var state = states && states.get($0);
        if (!state) return;
        state.stopped = true;
        state.running = false;
        if (state.processor) state.processor.disconnect();
        if (state.source) state.source.disconnect();
        if (state.ctx) state.ctx.close();
        if (state.stream) state.stream.getTracks().forEach(function(track) { track.stop(); });
        states.delete($0);
    }, webInstanceId_);
    running_ = false;
    logNotice("MicInput") << "stopped [Web]";
}

size_t MicInput::getBuffer(float* outBuffer, size_t numSamples) {
    if (!running_ || numSamples == 0) return 0;
    numSamples = std::min(numSamples, (size_t)BUFFER_SIZE);
    return (size_t)EM_ASM_INT({
        var state = Module.trusscMicStates && Module.trusscMicStates.get($0);
        if (!state || !state.running || state.stopped) return 0;
        var count = Math.min($2, state.buffer.length);
        var readPos = (state.writePos + state.buffer.length - count) % state.buffer.length;
        for (var i = 0; i < count; ++i)
            HEAPF32[($1 >> 2) + i] = state.buffer[(readPos + i) % state.buffer.length];
        return count;
    }, webInstanceId_, outBuffer, numSamples);
}

void MicInput::onAudioData(const float* input, size_t frameCount) {
    (void)input;
    (void)frameCount;
}

MicInput& getMicInput() {
    static MicInput instance;
    return instance;
}

} // namespace trussc
#endif // __EMSCRIPTEN__
