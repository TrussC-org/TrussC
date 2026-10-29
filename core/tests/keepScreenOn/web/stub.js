// Offline integration stub: real browser/device checks live elsewhere.
if (typeof process !== 'undefined' && require('node:worker_threads').isMainThread) {
    const assert = require('node:assert/strict');
    const listeners = new Set();
    let requests = 0, releases = 0;
    globalThis.isSecureContext = true;
    globalThis.document = {
        visibilityState: 'visible',
        addEventListener: (name, callback) => listeners.add(callback),
        removeEventListener: (name, callback) => listeners.delete(callback)
    };
    Object.defineProperty(globalThis, 'navigator', { configurable: true, value: {
        wakeLock: { request: type => {
            assert.equal(type, 'screen'); ++requests;
            return Promise.resolve({
                release: () => { ++releases; return Promise.resolve(); },
                addEventListener: () => {}
            });
        } }
    } });
    process.once('beforeExit', () => {
        assert.equal(requests, 1);
        assert.equal(releases, 1);
        assert.equal(listeners.size, 0);
        console.log('PASS linked Web Wake Lock acquisition and pending release');
    });
}
