// =============================================================================
// Web webcam capture: each run has its own never-reused JS identity.
// =============================================================================
#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include "TrussC.h"

EM_JS(void, tc_video_setup, (unsigned id, int width, int height), {
    Module.tcVideoGrabbers = Module.tcVideoGrabbers || new Map();
    const state = {stopped: false, ready: false, needsResize: false,
                   width: width, height: height, actualWidth: 0, actualHeight: 0,
                   buffer: 0, stream: null, video: null, canvas: null, ctx: null};
    Module.tcVideoGrabbers.set(id, state);
    navigator.mediaDevices.getUserMedia({video: {
        width: {ideal: width}, height: {ideal: height}
    }}).then(function(stream) {
        if (state.stopped) {
            stream.getTracks().forEach(function(track) { track.stop(); });
            return;
        }
        state.stream = stream;
        const video = document.createElement('video');
        state.video = video;
        video.setAttribute("playsinline", "");
        video.muted = true;
        video.style.display = 'none';
        document.body.appendChild(video);
        const canvas = document.createElement('canvas');
        state.canvas = canvas;
        state.ctx = canvas.getContext('2d', {willReadFrequently: true});
        video.onloadedmetadata = function() {
            if (state.stopped) return;
            state.actualWidth = video.videoWidth;
            state.actualHeight = video.videoHeight;
            canvas.width = state.actualWidth;
            canvas.height = state.actualHeight;
            state.needsResize = state.actualWidth !== state.width ||
                                state.actualHeight !== state.height;
            state.ready = true;
            video.play();
        };
        video.srcObject = stream;
    }).catch(function(error) {
        if (!state.stopped) console.error('[VideoGrabber] Web: failed to start -', error.message);
    });
});

EM_JS(void, tc_video_close, (unsigned id), {
    const state = Module.tcVideoGrabbers && Module.tcVideoGrabbers.get(id);
    if (!state) return;
    state.stopped = true;
    state.ready = false;
    state.buffer = 0;
    if (state.stream) state.stream.getTracks().forEach(function(track) { track.stop(); });
    if (state.video) {
        state.video.onloadedmetadata = null;
        state.video.pause();
        state.video.srcObject = null;
        state.video.remove();
    }
    state.stream = state.video = state.canvas = state.ctx = null;
    Module.tcVideoGrabbers.delete(id);
    // Outstanding promises retain this stopped state until they resolve.
});

EM_JS(void, tc_video_buffer, (unsigned id, unsigned char* buffer, int width, int height), {
    const state = Module.tcVideoGrabbers && Module.tcVideoGrabbers.get(id);
    if (!state || state.stopped) return;
    state.buffer = buffer;
    state.width = width;
    state.height = height;
});

EM_JS(int, tc_video_update, (unsigned id), {
    const state = Module.tcVideoGrabbers && Module.tcVideoGrabbers.get(id);
    if (!state || state.stopped || !state.ready || !state.buffer ||
        state.actualWidth !== state.width || state.actualHeight !== state.height) return 0;
    state.ctx.drawImage(state.video, 0, 0, state.width, state.height);
    const data = state.ctx.getImageData(0, 0, state.width, state.height).data;
    HEAPU8.set(data, state.buffer);
    return 1;
});

EM_JS(int, tc_video_size, (unsigned id, int field), {
    const state = Module.tcVideoGrabbers && Module.tcVideoGrabbers.get(id);
    if (!state || state.stopped) return 0;
    if (field === 0) return state.needsResize ? 1 : 0;
    if (field === 1) return state.actualWidth;
    return state.actualHeight;
});

EM_JS(void, tc_video_resized, (unsigned id), {
    const state = Module.tcVideoGrabbers && Module.tcVideoGrabbers.get(id);
    if (state) state.needsResize = false;
});

namespace trussc {

std::vector<VideoDeviceInfo> VideoGrabber::listDevicesPlatform() {
    return {{0, "Default Camera", "default"}};
}

bool VideoGrabber::setupPlatform() {
    static uint64_t nextId = 1;
    if (nextId > UINT32_MAX) {
        logError("VideoGrabber") << "Web capture identities exhausted";
        return false;
    }
    state_->webId = static_cast<uint32_t>(nextId++);
    state_->width = state_->requestedWidth;
    state_->height = state_->requestedHeight;
    deviceName_ = "Web Camera";
    tc_video_setup(state_->webId, state_->width, state_->height);
    return true;
}

void VideoGrabber::closePlatform() {
    if (state_->webId) tc_video_close(state_->webId);
}

void VideoGrabber::updatePlatform() {
    if (tc_video_update(state_->webId)) state_->dirty.store(true);
}

void VideoGrabber::updateDelegatePixels() {
    tc_video_buffer(state_->webId, state_->pixels.get(), state_->width, state_->height);
}

bool VideoGrabber::checkResizeNeeded() { return tc_video_size(state_->webId, 0) != 0; }
void VideoGrabber::getNewSize(int& width, int& height) {
    width = tc_video_size(state_->webId, 1);
    height = tc_video_size(state_->webId, 2);
}
void VideoGrabber::clearResizeFlag() { tc_video_resized(state_->webId); }
bool VideoGrabber::checkCameraPermission() { return true; }
void VideoGrabber::requestCameraPermission() {}

} // namespace trussc
#endif // __EMSCRIPTEN__
