const fs = require('fs');
const assert = require('assert/strict');
const source = fs.readFileSync('core/platform/web/tcVideoPlayer_web.cpp', 'utf8');
const script = source.match(/snprintf\(script, sizeof\(script\), R"JS\(([\s\S]*?)\)JS", pathStr.c_str\(\)\);/)[1].replace('%s', 'fixture.mp4');
global.window = {};
global.FS = { readFile() { throw Error('URL fixture'); } };
global.document = {
    body: { appendChild() {} },
    createElement(type) {
        if (type === 'canvas') return { getContext() { return {}; } };
        return { style: {}, setAttribute() {}, load() {}, remove() {}, pause() { this.paused = true; },
                 videoWidth: 64, videoHeight: 64, duration: 1 };
    }
};
Function(script)();
const video = window._trussc_player_video;
video.onloadedmetadata();
video.error = { message: 'decode failed', code: 3 };
video.onerror();
assert.deepEqual(window._trussc_player_error, video.error);
assert.equal(window._trussc_player_ready, true);
assert.equal(window._trussc_player_playing, false);
assert.equal(video.paused, true);
Function(script)();
assert.equal(window._trussc_player_error, null);
video.onerror();
assert.equal(window._trussc_player_error, null);
window._trussc_player_video.onerror();
assert.deepEqual(window._trussc_player_error, { message: 'Video playback failed', code: 0 });
console.log('Web error handler: message/code, pause, loaded state, reload reset, stale event, fallback PASS');
