// Executes the actual EM_JS ingress with a deterministic browser SDK stand-in.
// This checks admission/copy ownership, not browser codec internals.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync('core/platform/web/tcSound_web.cpp', 'utf8');
function body(name) {
    const start = source.search(new RegExp('EM_JS\\([^,]+, ' + name + ','));
    const begin = source.indexOf('{', start);
    return source.slice(begin + 1, source.indexOf('\n});', begin));
}
async function run({size = 12, channels = 2, rate = 44100, length = 8, highPointer = false} = {}) {
    let reads = 0, decodes = 0, copies = 0;
    const context = {Module: {}, Map, Number, UTF8ToString: () => 'clip.m4a',
        FS: {stat: () => ({size}), readFile: () => { ++reads; return new Uint8Array(12); }},
        OfflineAudioContext: class {
            decodeAudioData() {
                ++decodes;
                return Promise.resolve({numberOfChannels: channels, sampleRate: rate, length,
                    getChannelData: ch => { ++copies; return Float32Array.from({length}, (_, i) => ch * 10 + i); }});
            }
        }};
    vm.createContext(context);
    vm.runInContext('function decodeAacFile(pathPtr, encodedCap, channelCap, rateCap) {' + body('decodeAacFile') + '}', context);
    const id = context.decodeAacFile(0, 64, 32, 384000);
    await Promise.resolve(); await Promise.resolve();
    const slot = context.Module.trusscAacSlots.get(id);
    if (slot.success) {
        const writes = [];
        context.HEAPF32 = highPointer ? new Proxy({}, {set: (_, key) => { writes.push(Number(key)); return true; }}) : new Float32Array(32);
        vm.runInContext('function copyAacData(slot, outPtr, totalSamples) {' + body('copyAacData') + '}', context);
        context.copyAacData(id, highPointer ? 0x80000000 : 0, channels * length);
        if (highPointer) assert.deepEqual(writes.slice(0, 2), [0x20000000, 0x20000002]);
        else assert.deepEqual(Array.from(context.HEAPF32.slice(0, 6)), [0, 10, 1, 11, 2, 12]);
    }
    return {slot: context.Module.trusscAacSlots.get(id), reads, decodes, copies};
}
(async () => {
    let r = await run({size: 65});
    assert.equal(r.reads, 0); assert.equal(r.decodes, 0); assert.ok(r.slot.bound);
    r = await run({highPointer: true});
    assert.equal(r.slot.success, true);
    r = await run(); // exactly 64 bytes of decoded audio
    assert.equal(r.slot.success, true); assert.equal(r.slot.data.length, 8);
    assert.equal(r.copies, 2); // one direct channel access per channel
    r = await run({length: 9}); // decoded PCM has no quota
    assert.equal(r.slot.success, true);
    for (const metadata of [ {channels: 33}, {rate: 384001}, {rate: NaN}, {channels: 1.5}, {length: -1}]) {
        r = await run(metadata);
        assert.ok(r.slot.bound); assert.equal(r.slot.data, null); assert.equal(r.copies, 0);
    }
    console.log('PASS: Web admission and AudioBuffer ownership (SDK stand-in)');
})().catch(e => { console.error(e); process.exitCode = 1; });
