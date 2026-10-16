#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "tc/sound/tcMicInputInternal.h"
#include <map>

namespace trussc {
namespace internal {
namespace {
unsigned nextMicKey = 0;
auto& micStates() {
    static auto* states = new std::map<unsigned, std::shared_ptr<MicState>>;
    return *states;
}
using ReadyCallback = void (*)(unsigned, int, const char*, int);
using AudioCallback = void (*)(unsigned, const float*, int);
void ready(unsigned key, int success, const char* message, int rate) {
    auto found = micStates().find(key);
    if (found == micStates().end()) return;
    auto state = found->second;
    state->result = {success != 0, message};
    if (success) state->sampleRate = rate;
    completeMic(state);
}
void audio(unsigned key, const float* samples, int frames) {
    auto found = micStates().find(key);
    if (found != micStates().end() && !found->second->cancelled)
        found->second->audio(samples, frames);
}
EM_JS_DEPS(micAsyncDeps, "$getWasmTableEntry,$stringToNewUTF8");
EM_JS(void, beginWebMic, (unsigned key, int rate, int frames, ReadyCallback readyFn, AudioCallback audioFn), {
    const states = Module.trusscMicStates || (Module.trusscMicStates = new Map());
    const state = { stopped: false, delivered: false };
    states.set(key, state);
    state.cleanup = () => {
        if (state.stopped) return;
        state.stopped = true;
        const release = fn => { try { fn(); } catch (error) {} };
        if (state.processor) { state.processor.onaudioprocess = null; release(() => state.processor.disconnect()); }
        if (state.source) release(() => state.source.disconnect());
        if (state.stream) state.stream.getTracks().forEach(track => release(() => track.stop()));
        if (state.ctx) { state.ctx.onstatechange = null; release(() => state.ctx.close().catch(() => {})); }
        if (state.samples) { _free(state.samples); state.samples = 0; }
    };
    const finish = (success, message) => {
        if (state.stopped || state.delivered) return;
        state.delivered = true;
        if (state.ctx) state.ctx.onstatechange = null;
        if (!success) state.cleanup();
        const text = stringToNewUTF8(message);
        try { getWasmTableEntry(readyFn)(key, success, text, state.ctx ? state.ctx.sampleRate : rate); }
        finally { _free(text); }
    };
    const fail = error => finish(0, (error.name || 'Error') + ': ' + (error.message || error) +
        '; retry microphone startup from a user gesture in a secure context');
    try {
        // Snapshot before async permission/resume work consumes activation.
        const activated = navigator.userActivation ? navigator.userActivation.isActive : undefined;
        // Both operations start in the initiating gesture, before any deferral.
        state.ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: rate });
        const connect = () => {
            if (state.stopped || state.delivered || !state.stream || state.ctx.state !== 'running') return;
            try {
                state.source = state.ctx.createMediaStreamSource(state.stream);
                state.processor = state.ctx.createScriptProcessor(frames, 1, 1);
                state.samples = _malloc(frames * 4);
                state.processor.onaudioprocess = event => {
                    if (state.stopped) return;
                    const input = event.inputBuffer.getChannelData(0);
                    HEAPF32.set(input, state.samples >> 2);
                    getWasmTableEntry(audioFn)(key, state.samples, input.length);
                };
                state.source.connect(state.processor);
                state.processor.connect(state.ctx.destination);
                finish(1, "");
            } catch (error) { fail(error); }
        };
        state.ctx.onstatechange = connect;
        Promise.resolve(state.ctx.resume()).then(connect, fail);
        navigator.mediaDevices.getUserMedia({ audio: {
            sampleRate: rate, channelCount: 1, echoCancellation: false,
            noiseSuppression: false, autoGainControl: false
        }}).then(stream => {
            if (state.stopped) { stream.getTracks().forEach(track => track.stop()); return; }
            state.stream = stream;
            if (activated === false && state.ctx.state === 'suspended') {
                fail(new Error('Microphone audio needs a user gesture: AudioContext is still suspended'));
                return;
            }
            // Permitted startup (or unknown activation on older browsers) can
            // take arbitrarily long. Resume or a running statechange connects;
            // stop/destroy still release everything immediately.
            connect();
        }).catch(fail);
    } catch (error) { fail(error); }
});
}
void launchMic(const std::shared_ptr<MicState>& state, bool) {
    do { state->webKey = ++nextMicKey; } while (!state->webKey || micStates().contains(state->webKey));
    micStates().emplace(state->webKey, state);
    beginWebMic(state->webKey, state->sampleRate, MicInput::BUFFER_SIZE, ready, audio);
}
void cancelMic(const std::shared_ptr<MicState>& state) {
    state->cancelled = true;
    EM_ASM({
        const states = Module.trusscMicStates;
        const state = states && states.get($0);
        if (state) { state.cleanup(); states.delete($0); }
    }, state->webKey);
    micStates().erase(state->webKey);
    state->predecessor.reset();
}
void setMicBackendForTests(MicOpen, MicClose) {}
}
int MicInput::selectedBackend() { return -1; }
}
#endif
