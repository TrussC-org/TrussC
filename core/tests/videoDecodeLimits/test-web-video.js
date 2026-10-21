// Execute the actual embedded JS with real Blob snapshots and a UTF-8 heap.
// Browser decoding/canvas internals are deliberately not simulated.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const source = fs.readFileSync(process.env.VIDEO_WEB_SOURCE ||
    path.join(__dirname, '../../platform/web/tcVideoPlayer_web.cpp'), 'utf8');
const fixed = source.match(/EM_JS\(int, tcVideoBeginLoad,[^\n]*\{([\s\S]*?)^\}\);/m);
const legacy = source.match(/snprintf\(script, sizeof\(script\), R"JS\(([\s\S]*?)\)JS"/);
function load({width = 32, height = 16, kind = 'memfs', size = 16, url = 'movie.mp4', failBlob = false, grownSize = size, shortRead = 1048576, realFile} = {}) {
    let canvases = 0, reads = 0, loads = 0, opens = 0, closes = 0, readFileCalls = 0, maxReadBuffer = 0, blobs = [], objectBlob;
    const allocations = [];
    class ObservedArray extends Uint8Array {
        constructor(value, ...args) {
            super(value, ...args);
            if (typeof value === 'number') allocations.push(value);
        }
    }
    class ObservedBlob extends Blob {
        constructor(parts, options) {
            if (failBlob) throw new RangeError('injected Blob allocation failure');
            super(parts, options);
            blobs.push(this);
        }
    }
    const contents = kind === 'workerfs' ? new ObservedBlob(['video']) :
        new Uint8Array(kind === 'memfs' ? size + 8 : 16).fill(23);
    blobs = [];
    const video = {
        videoWidth: width, videoHeight: height, duration: 100000,
        setAttribute() {}, pause() {}, load() { ++loads; }, style: {},
    };
    const heap = Buffer.concat([Buffer.alloc(32), Buffer.from(url, 'utf8'), Buffer.from([0])]);
    const context = {
        pathPtr: 32, pathLength: Buffer.byteLength(url),
        UTF8ToString(ptr, length) { return heap.subarray(ptr, ptr + length).toString('utf8'); },
        window: {}, Blob: ObservedBlob, Uint8Array: ObservedArray,
        URL: {createObjectURL(blob) { objectBlob = blob; return 'blob:video'; }, revokeObjectURL() {}},
        document: {body: {appendChild() {}}, createElement(type) {
            if (type === 'video') return video;
            ++canvases;
            return {getContext() { return {}; }};
        }},
        FS: {lookupPath() {
            if (kind === 'url') throw Error('not in FS');
            return {node: {contents: kind === 'other' ? {} : contents, usedBytes: size}};
        }, stat() { return {size: realFile ? fs.statSync(realFile).size : (opens ? grownSize : size)}; },
            open() {
                ++opens;
                if (realFile) {
                    fs.truncateSync(realFile, grownSize); // writer runs after preflight
                    return {fd: fs.openSync(realFile, 'r')};
                }
                return {fd: 0, position: 0};
            },
            fstat(fd) { return {size: realFile ? fs.fstatSync(fd).size : grownSize}; },
            read(stream, buffer, offset, length) {
                ++reads;
                maxReadBuffer = Math.max(maxReadBuffer, buffer.byteLength);
                if (realFile) return fs.readSync(stream.fd, buffer, offset, length, null);
                const count = Math.min(length, shortRead, grownSize - stream.position);
                buffer.fill(23, offset, offset + count);
                stream.position += count;
                return count;
            },
            close(stream) { ++closes; if (realFile) fs.closeSync(stream.fd); },
            // Model the SDK whole-file route for regression sensitivity:
            // open runs the growth hook, then a fresh stat sizes its allocation.
            readFile(file) {
                ++readFileCalls;
                const stream = this.open(file, 'r');
                try {
                    const data = new ObservedArray(this.stat(file).size);
                    this.read(stream, data, 0, data.length);
                    return data;
                } finally { this.close(stream); }
            }},
        console: {log() {}},
    };
    // For the previous round, reproduce the real snprintf capacity, too.
    const script = fixed ? `(function(pathPtr, pathLength) {${fixed[1]}})(pathPtr, pathLength)` :
        Buffer.from(legacy[1].replace('%s', url)).subarray(0, 8191).toString();
    assert.equal(vm.runInNewContext(script, context), 1);
    return {context, video, contents, blobs, objectBlob,
        counts() { return {canvases, reads, loads, opens, closes, readFileCalls, maxReadBuffer, maxAllocation: Math.max(0, ...allocations)}; }};
}
let failures = 0;
async function test(name, run) {
    try { await run(); console.log(`${name}: PASS`); }
    catch (error) { ++failures; console.error(`${name}: FAIL: ${error.message}`); }
}
async function main() {
    await test('Web metadata admission', () => {
        for (const [width, height, accepted] of [[7680,4320,true], [8192,4096,true], [8192,4097,false], [8193,1,false]]) {
            const result = load({width, height});
            result.video.onloadedmetadata();
            assert.equal(result.context.window._trussc_player_ready, accepted);
            assert.equal(result.counts().canvases, accepted ? 1 : 0);
            if (!accepted) assert.equal(result.context.window._trussc_player_error.code, -863);
        }
    });
    await test('MEMFS makes exactly one file-sized Blob snapshot, without readFile or a new cap', async () => {
        const size = 65 * 1024 * 1024;
        const result = load({size});
        assert.equal(result.counts().reads, 0);
        assert.equal(result.blobs.length, 1);
        assert.equal(result.objectBlob, result.blobs[0]);
        assert.equal(result.objectBlob.size, size); // usedBytes, not backing capacity
        result.contents[0] = 42;
        assert.equal(new Uint8Array(await result.objectBlob.slice(0, 1).arrayBuffer())[0], 23);
    });
    await test('WORKERFS reuses its existing Blob with no copy', () => {
        const result = load({kind: 'workerfs'});
        assert.equal(result.counts().reads, 0);
        assert.equal(result.blobs.length, 0);
        assert.equal(result.objectBlob, result.contents);
    });
    await test('MEMFS Blob allocation failure is a structured load failure', () => {
        const result = load({failBlob: true});
        assert.equal(result.context.window._trussc_player_error.code, -864);
        assert.match(result.context.window._trussc_player_error.message, /staging failed.*allocation failure/);
        assert.equal(result.counts().loads, 0);
        assert.equal(result.video.src, undefined);
    });
    await test('16 KiB URL and UTF-8/quotes reach the loader intact', () => {
        for (const url of [
            'https://example.invalid/movie?token=' + 'x'.repeat(16384),
            'https://example.invalid/movie?token=' + 'x'.repeat(3502),
            "https://example.invalid/movie?name=映像'\\\n",
        ]) {
            const result = load({kind: 'url', url});
            assert.equal(result.video.src, url);
            assert.equal(result.counts().loads, 1);
        }
    });
    await test('Staging rejection carries its store, needed bytes and actual capacity across JS getters', () => {
        for (const needed of [65 * 1024 * 1024, 2 ** 32 + 1]) {
            const result = load({kind: 'other', size: needed});
            assert.equal(result.counts().reads, 0);
            assert.equal(result.counts().loads, 0);
            const error = result.context.window._trussc_player_error;
            assert.equal(error.code, -863);
            assert.equal(error.store, 'staging');
            for (const [name, value] of [['Needed', needed], ['Capacity', 67108864], ['IsStaging', true]]) {
                const getter = source.match(new RegExp(`EM_JS\\([^,]+, tcVideoError${name}, \\(\\), \\{([^}]+)\\}`));
                assert.ok(getter, `missing ${name} bridge`);
                assert.equal(vm.runInNewContext(`(function(){${getter[1]}})()`, result.context), value);
            }
        }
    });
    await test('Other filesystem growth rejects before a grown-size allocation or Blob', () => {
        const dir = fs.mkdtempSync(path.join(require('node:os').tmpdir(), 'tc-video-growth-'));
        const realFile = path.join(dir, 'movie.mp4');
        fs.writeFileSync(realFile, Buffer.alloc(16));
        let result;
        try { result = load({kind: 'other', size: 16, grownSize: 65 * 1024 * 1024, realFile}); }
        finally { fs.rmSync(dir, {recursive: true, force: true}); }
        const counts = result.counts();
        assert.equal(counts.readFileCalls, 0, 'whole-file readFile must never be used');
        assert.equal(counts.opens, 1);
        assert.equal(counts.closes, 1);
        assert.ok(counts.maxReadBuffer <= 1048576);
        assert.ok(counts.maxAllocation <= 1048576, 'no allocation may use the grown file size');
        assert.equal(counts.loads, 0);
        assert.equal(result.blobs.length, 0);
        assert.equal(result.video.src, undefined);
        const error = result.context.window._trussc_player_error;
        assert.equal(error.code, -863);
        assert.equal(error.store, 'staging');
        assert.equal(error.capacity, 67108864);
        assert.equal(error.needed, 65 * 1024 * 1024);
    });
    await test('Other filesystem bounded reads admit the complete exact limit and short reads', async () => {
        for (const [size, shortRead] of [[67108864, 1048576], [1048583, 127], [0, 1048576]]) {
            const result = load({kind: 'other', size, shortRead});
            const counts = result.counts();
            assert.equal(counts.readFileCalls, 0);
            assert.equal(counts.opens, 1);
            assert.equal(counts.closes, 1);
            assert.ok(counts.maxReadBuffer <= 1048576);
            assert.ok(counts.maxAllocation <= 1048576);
            assert.equal(result.blobs.length, 1);
            assert.equal(result.objectBlob.size, size);
            const state = result.context.window;
            assert.equal(state._trussc_player_staging_bytes, size);
            assert.ok(state._trussc_player_staging_peak >= size * 2, 'chunks and Blob both counted');
            assert.ok(state._trussc_player_staging_peak <= 134217728, 'all staging capacity stays bounded');
            const data = new Uint8Array(await result.objectBlob.arrayBuffer());
            assert.ok(data.every(byte => byte === 23));
        }
    });
    const scripts = [...source.matchAll(/snprintf\(script, sizeof\(script\), R"JS\(([\s\S]*?)\)JS"/g)];
    const copyScript = scripts[fixed ? 0 : 1][1].replace('%d', '32').replace('%d', '16').replace('%u', '0');
    await test('Web size/layout changes immediately before copy leave the mirror intact', () => {
        for (const [width, height, result, rejected, deliveredBytes] of [
            [32,16,1,false,2048], [64,32,0,false,2048], [8193,1,0,true,2048],
            [32,16,0,true,2047], [32,16,0,true,2049]]) {
            let draws = 0;
            const state = {
                _trussc_player_video: {videoWidth: width, videoHeight: height, readyState: 2, pause() {}},
                _trussc_player_ready: true,
                _trussc_player_canvas: {width: 32, height: 16},
                _trussc_player_ctx: {drawImage() { ++draws; }, getImageData() { return {data: new Uint8Array(deliveredBytes)}; }},
            };
            const heap = new Uint8Array(2048).fill(7);
            assert.equal(vm.runInNewContext(copyScript, {window: state, HEAPU8: heap}), result);
            assert.equal(draws, width === 32 && height === 16 ? 1 : 0);
            if (!result) assert.ok(heap.every(value => value === 7));
            assert.equal(!!state._trussc_player_error, rejected);
            if (rejected) assert.equal(state._trussc_player_error.code, -863);
        }
    });
    process.exitCode = failures ? 1 : 0;
}
main().catch(error => { console.error(error); process.exitCode = 1; });
