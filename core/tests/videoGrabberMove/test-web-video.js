// Execute the actual EM_JS bodies (or the emitted canary functions) with a
// deterministic getUserMedia/DOM mock. Run from the repository root.
const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const emittedPath = process.argv[2];
const source = fs.readFileSync(emittedPath || 'core/platform/web/tcVideoGrabber_web.cpp', 'utf8');
const functions = {};
if (!emittedPath) {
    const pattern = /EM_JS\([^,]+, (tc_video_\w+), \((.*?)\), \{([\s\S]*?)\n\}\);/g;
    for (const match of source.matchAll(pattern)) {
        const params = match[2].split(',').map(p => p.trim().split(/\s+/).pop().replace('*', ''));
        functions[match[1]] = {params, body: match[3]};
    }
} else {
    for (const name of ['setup', 'close', 'buffer', 'update', 'size', 'resized']) {
        const functionName = 'tc_video_' + name;
        const pattern = new RegExp('function _?' + functionName + '\\(([^)]*)\\)\\s*\\{', 'g');
        const match = pattern.exec(source);
        assert(match, 'emitted function exists: ' + functionName);
        let depth = 1, end = pattern.lastIndex;
        // These EM_JS bodies contain no brace characters inside strings.
        while (depth && end < source.length) {
            if (source[end] === '{') ++depth;
            if (source[end] === '}') --depth;
            ++end;
        }
        functions[functionName] = {params: match[1].split(','), body: source.slice(pattern.lastIndex, end - 1)};
    }
}
assert.strictEqual(Object.keys(functions).length, 6);
const pending = [];
const videos = [];
let created = 0, playCalls = 0;
const context = vm.createContext({
    Module: {}, HEAPU8: new Uint8Array(128), console,
    navigator: {mediaDevices: {getUserMedia() {
        return new Promise(resolve => pending.push(resolve));
    }}},
    document: {body: {appendChild() {}}, createElement(kind) {
        ++created;
        if (kind === 'video') {
            const video = {videoWidth: 1, videoHeight: 1, style: {}, setAttribute() {},
                play() { ++playCalls; }, pause() {}, remove() { this.removed = true; }};
            videos.push(video);
            return video;
        }
        return {width: 0, height: 0, getContext() {
            let marker = 0;
            return {drawImage(video) { marker = video.srcObject.marker; },
                getImageData() { return {data: new Uint8Array([marker, 2, 3, 255])}; }};
        }};
    }}
});
for (const [name, {params, body}] of Object.entries(functions))
    vm.runInContext(`function ${name}(${params.join(',')}) {${body}\n}`, context);
function stream(marker) {
    return {marker, stops: 0, getTracks() { return [{stop: () => { ++this.stops; }}]; }};
}
async function resolve(index, captureStream) {
    pending[index](captureStream);
    await Promise.resolve();
    await Promise.resolve();
}
(async () => {
    context.tc_video_setup(1, 1, 1);
    context.tc_video_setup(2, 1, 1);
    context.tc_video_close(1);
    const lateStream = stream(11);
    await resolve(0, lateStream);
    assert.strictEqual(lateStream.stops, 1);
    assert.strictEqual(created, 0, 'late resolution creates no element');
    const secondStream = stream(22);
    await resolve(1, secondStream);
    const metadata = videos[0].onloadedmetadata;
    metadata();
    context.tc_video_buffer(2, 16, 1, 1);
    assert.strictEqual(context.tc_video_update(2), 1);
    assert.strictEqual(context.HEAPU8[16], 22);
    assert.strictEqual(secondStream.stops, 0, 'other instance remains live');

    context.tc_video_setup(3, 1, 1);
    const thirdStream = stream(33);
    await resolve(2, thirdStream);
    videos[1].onloadedmetadata();
    context.tc_video_buffer(3, 32, 1, 1);
    context.tc_video_update(3);
    assert.strictEqual(context.HEAPU8[32], 33);
    assert.strictEqual(context.HEAPU8[16], 22, 'instances have independent buffers');
    context.tc_video_close(2);
    const playsBefore = playCalls;
    metadata();
    assert.strictEqual(playCalls, playsBefore, 'late metadata cannot restart stopped video');
    assert.strictEqual(secondStream.stops, 1);
    assert.strictEqual(context.tc_video_update(2), 0);
    assert.strictEqual(context.tc_video_update(3), 1);

    // A first frame with a different negotiated size must wait for C++ resize.
    videos[1].videoWidth = 2;
    videos[1].onloadedmetadata();
    assert.strictEqual(context.tc_video_size(3, 0), 1);
    assert.strictEqual(context.tc_video_size(3, 1), 2);
    assert.strictEqual(context.tc_video_update(3), 0);
    context.tc_video_buffer(3, 48, 2, 1);
    context.tc_video_resized(3);
    assert.strictEqual(context.tc_video_update(3), 1);
    assert.strictEqual(context.HEAPU8[48], 33);

    context.tc_video_setup(4, 1, 1);
    context.tc_video_close(4);
    context.tc_video_setup(5, 1, 1);
    const replacement = stream(55), oldStream = stream(44);
    await resolve(4, replacement);
    await resolve(3, oldStream);
    assert.strictEqual(oldStream.stops, 1);
    assert.strictEqual(replacement.stops, 0, 'old promise cannot stop replacement');
    assert.strictEqual(context.Module.tcVideoGrabbers.has(4), false);
    // The camera opens at a different size than requested: no copy into the
    // requested-size buffer until the handle reallocates, then frames resume.
    context.tc_video_setup(6, 1, 1);
    const resizedStream = stream(66);
    await resolve(5, resizedStream);
    const opened = videos[videos.length - 1];
    opened.videoWidth = 2;
    opened.onloadedmetadata();
    context.tc_video_buffer(6, 64, 1, 1);
    assert.strictEqual(context.tc_video_size(6, 0), 1, 'needsResize set');
    assert.strictEqual(context.tc_video_size(6, 1), 2, 'actual width reported');
    assert.strictEqual(context.tc_video_size(6, 2), 1);
    assert.strictEqual(context.tc_video_update(6), 0, 'no copy into the requested-size buffer');
    assert.strictEqual(context.HEAPU8[64], 0);
    context.tc_video_buffer(6, 80, 2, 1);
    context.tc_video_resized(6);
    assert.strictEqual(context.tc_video_size(6, 0), 0, 'needsResize cleared');
    assert.strictEqual(context.tc_video_update(6), 1, 'updates resume after reallocation');
    assert.strictEqual(context.HEAPU8[80], 66);
    assert.strictEqual(context.tc_video_update(6), 1, 'and keep running');
    context.tc_video_close(6);

    context.tc_video_close(3);
    context.tc_video_close(5);
    assert.strictEqual(context.Module.tcVideoGrabbers.size, 0);
    console.log('PASS: late resolution, independent capture, metadata stop, resize, open-at-other-size and restart');
})().catch(error => { console.error(error); process.exitCode = 1; });
