// Run the actual EM_JS bodies against Node's real byte streams. No network.
const fs = require('fs');
const assert = require('assert');
const path = require('path');
const source = fs.readFileSync(path.join(__dirname,
    '../../../core/include/tc/graphics/tcBoundedFontFetch.cpp'), 'utf8');
const start = new Function('url', 'request', 'timeout', 'limit', 'done', source.split('void* done), {\n')[1].split('\n});')[0]);
const cancel = new Function('request', source.split('EM_JS(void, cancelFontStream, (void* request), {\n')[1].split('\n});')[0]);
const limit = 131072;
global.Module = {};
global.UTF8ToString = value => value;
global.HEAPU8 = new Uint8Array(limit + 16);
let allocated, freed, reads, completion, peakCapacity, peakChunks;
global._malloc = bytes => { allocated += bytes; return 8; };
global._free = () => { ++freed; };
global.getWasmTableEntry = () => completion;

async function run(total, {declared = null, oneByte = false, noByob = false, block = false} = {}) {
    allocated = freed = reads = peakCapacity = peakChunks = 0;
    if (block) total = 4096;
    let consumed = 0, pending, cancelled = false;
    const body = new ReadableStream({
        // A real byte stream, including a no-BYOB default-stream control.
        ...(noByob ? {} : {type: 'bytes'}),
        pull(controller) {
            ++reads;
            const entry = Module.tcFontStreams.get(7);
            if (entry) {
                const capacity = entry.chunks.reduce((n, c) => n + c.buffer.byteLength, 0);
                peakCapacity = Math.max(peakCapacity, capacity);
                peakChunks = Math.max(peakChunks, entry.chunks.length);
                if (entry.capacity !== undefined) {
                    assert.strictEqual(entry.capacity, capacity);
                    assert.ok(entry.credits >= capacity + entry.chunks.length * 1024);
                }
            }
            if (block && consumed > 0) return new Promise(resolve => { pending = resolve; });
            if (consumed === total) { controller.close(); return; }
            const count = Math.min(oneByte ? 1 : 16384, total - consumed);
            consumed += count;
            // Real byte streams transfer the backing buffer; retaining a short
            // ingress view must never retain this 16 KiB per byte.
            const backing = new Uint8Array(16384);
            backing.fill(97);
            controller.enqueue(backing.subarray(0, count));
        },
        cancel() { cancelled = true; if (pending) pending(); }
    }, {highWaterMark: 0});
    global.fetch = async () => ({
        ok: true, status: 200, headers: {get: () => declared}, body,
        arrayBuffer() { throw Error('completed-response buffering forbidden'); }
    });
    let done = false;
    const result = new Promise(resolve => {
        completion = (request, buffer, size, status) => {
            done = true;
            const bytes = buffer ? Array.from(HEAPU8.subarray(buffer, buffer + size)) : [];
            if (buffer) _free(buffer);
            resolve({size, status, bytes});
        };
    });
    start('local-fixture', 7, 10000, limit, 1);
    if (block) {
        while (!pending) await new Promise(resolve => setImmediate(resolve));
        const entry = Module.tcFontStreams.get(7);
        assert.strictEqual(entry.capacity, 65536);
        assert.ok(entry.chunks.length > 0);
        cancel(7);
        assert.strictEqual(entry.capacity, 0);
        assert.strictEqual(entry.credits, 0);
        assert.strictEqual(entry.chunks.length, 0);
        await new Promise(resolve => setImmediate(resolve));
        assert.strictEqual(done, false);
        assert.strictEqual(Module.tcFontStreams.size, 0);
        assert.strictEqual(allocated, 0);
        assert.ok(cancelled);
        return;
    }
    const value = await result;
    assert.strictEqual(Module.tcFontStreams.size, 0);
    assert.ok(peakCapacity <= Math.ceil(limit / 65536) * 65536);
    assert.ok(peakChunks <= Math.ceil(limit / 65536));
    if (value.status === -1) assert.ok(cancelled);
    return value;
}
(async () => {
    let result = await run(4096, {oneByte: true});
    assert.strictEqual(result.size, 4096);
    assert.strictEqual(result.status, 200);
    assert.strictEqual(peakCapacity, 65536);
    assert.strictEqual(peakChunks, 1);
    result = await run(limit);
    assert.strictEqual(result.status, 200);
    assert.strictEqual(result.size, limit);
    assert.ok(result.bytes.every(value => value === 97));
    assert.strictEqual(allocated, limit);
    assert.strictEqual(freed, 1);
    result = await run(12, {noByob: true});
    assert.strictEqual(result.status, 200);
    assert.strictEqual(result.size, 12);
    for (const declared of [null, '12']) {
        result = await run(limit + 1, {declared});
        assert.strictEqual(result.status, -1);
        assert.strictEqual(result.size, 0);
        assert.strictEqual(allocated, 0);
    }
    result = await run(10, {declared: String(limit + 1)});
    assert.strictEqual(result.status, -1);
    assert.strictEqual(reads, 0);
    await run(0, {block: true});
    result = await run(2);
    assert.strictEqual(result.size, 2);
    console.log('PASS: real byte stream short reads, fixed block capacity/bookkeeping, default reader, limits, cancellation and retry');
})().catch(error => { console.error(error); process.exitCode = 1; });
