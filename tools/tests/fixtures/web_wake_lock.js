const assert = require('node:assert/strict');
const vm = require('node:vm');
const flush = async () => { for (let i = 0; i < 12; ++i) await Promise.resolve(); };
function deferred() {
    let resolve, reject;
    const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
    return { promise, resolve, reject };
}
function harness({ secure = true, supported = true, hidden = false } = {}) {
    const listeners = new Set(), requests = [], sentinels = [];
    let notices = 0, peak = 0, active = 0;
    const document = {
        visibilityState: hidden ? 'hidden' : 'visible',
        addEventListener: (name, callback) => { assert.equal(name, 'visibilitychange'); listeners.add(callback); },
        removeEventListener: (name, callback) => listeners.delete(callback)
    };
    const navigator = supported ? { wakeLock: { request: type => {
        assert.equal(type, 'screen');
        const wait = deferred(); requests.push(wait); return wait.promise;
    } } } : {};
    const Module = {};
    const context = vm.createContext({ Module, document, navigator, isSecureContext: secure });
    const apply = vm.runInContext('(enabled) => {\n' + productionBody + '\n}', context);
    function sentinel() {
        let callback;
        const value = {
            released: false, releases: 0, delay: null,
            addEventListener: (name, fn) => { assert.equal(name, 'release'); callback = fn; },
            drop: (notify = true) => {
                if (!value.released) { value.released = true; --active; }
                if (callback && notify) callback();
            },
            release: () => { ++value.releases; value.drop(); return value.delay || Promise.resolve(); }
        };
        ++active; peak = Math.max(peak, active); sentinels.push(value);
        return value;
    }
    return { apply: value => { const notice = apply(value); assert.ok(notice === 0 || notice === 1); notices += notice; return notice; }, Module, requests, listeners, navigator,
        sentinel, sentinels, get notices() { return notices; }, get peak() { return peak; },
        visible: value => {
            document.visibilityState = value ? 'visible' : 'hidden';
            for (const callback of listeners) callback();
        }
    };
}
async function run() {
    let count = 0;
    async function check(name, test) { await test(); ++count; console.log('PASS ' + name); }
    await check('unused false allocates no JS state', async () => {
        const h = harness(); h.apply(false); assert.equal(h.Module.trusscWakeLock, undefined);
    });
    await check('acquire, idempotence, release, listener removal', async () => {
        const h = harness(); h.apply(true); h.apply(true);
        assert.equal(h.requests.length, 1); assert.equal(h.listeners.size, 1);
        const s = h.sentinel(); h.requests[0].resolve(s); await flush();
        h.apply(false); h.apply(false); await flush();
        assert.equal(s.releases, 1); assert.equal(h.listeners.size, 0);
    });
    await check('false while acquisition is pending', async () => {
        const h = harness(); h.apply(true); h.apply(false);
        const s = h.sentinel(); h.requests[0].resolve(s); await flush();
        assert.equal(s.releases, 1); assert.equal(h.requests.length, 1);
    });
    await check('true false true pending acquisition never overlaps sentinels', async () => {
        const h = harness(); h.apply(true); h.apply(false); h.apply(true);
        const first = h.sentinel(); const release = deferred(); first.delay = release.promise;
        h.requests[0].resolve(first); await flush(); assert.equal(h.requests.length, 1);
        release.resolve(); await flush(); assert.equal(h.requests.length, 2);
        const second = h.sentinel(); h.requests[1].resolve(second); await flush();
        assert.equal(first.releases, 1); assert.equal(h.peak, 1);
        assert.equal(h.Module.trusscWakeLock.sentinel, second);
    });
    await check('re-enable waits for explicit release completion', async () => {
        const h = harness(); h.apply(true); const s = h.sentinel();
        h.requests[0].resolve(s); await flush(); const release = deferred(); s.delay = release.promise;
        h.apply(false); h.apply(true); await flush(); assert.equal(h.requests.length, 1);
        release.resolve(); await flush(); assert.equal(h.requests.length, 2);
    });
    await check('hidden request waits for visibility', async () => {
        const h = harness({ hidden: true }); h.apply(true); assert.equal(h.requests.length, 0);
        h.visible(true); assert.equal(h.requests.length, 1);
    });
    await check('browser drop and visibility restore re-acquire', async () => {
        const h = harness(); h.apply(true); const s = h.sentinel(); h.requests[0].resolve(s); await flush();
        s.drop(); h.visible(false); await flush(); h.visible(true); await flush();
        assert.equal(h.requests.length, 2); assert.equal(h.listeners.size, 1);
    });
    await check('hide while pending releases result and restore re-acquires', async () => {
        const h = harness(); h.apply(true); h.visible(false);
        const s = h.sentinel(); h.requests[0].resolve(s); await flush(); assert.equal(s.releases, 1);
        h.visible(true); assert.equal(h.requests.length, 2);
    });
    await check('visible event before old release notification re-acquires safely', async () => {
        const h = harness(); h.apply(true); const old = h.sentinel();
        h.requests[0].resolve(old); await flush(); old.drop(false); h.visible(true);
        assert.equal(h.requests.length, 2); const current = h.sentinel();
        h.requests[1].resolve(current); await flush(); old.drop();
        assert.equal(h.Module.trusscWakeLock.sentinel, current); assert.equal(h.peak, 1);
    });
    await check('hide/show during pending request retires old generation', async () => {
        const h = harness(); h.apply(true); h.visible(false); h.visible(true);
        const s = h.sentinel(); h.requests[0].resolve(s); await flush();
        assert.equal(s.releases, 1); assert.equal(h.requests.length, 2);
    });
    await check('false stops visibility retries', async () => {
        const h = harness({ hidden: true }); h.apply(true); h.apply(false); h.visible(true);
        assert.equal(h.requests.length, 0); assert.equal(h.listeners.size, 0);
    });
    for (const [name, options] of [['insecure context', { secure: false }], ['missing API', { supported: false }]]) {
        await check(name + ' reports once then no-ops', async () => {
            const h = harness(options); h.apply(true); h.apply(false); h.apply(true); h.visible(true);
            assert.equal(h.notices, 1); assert.equal(h.requests.length, 0); assert.equal(h.listeners.size, 0);
        });
    }
    await check('hidden NotAllowedError rejection retries when visible with a live listener', async () => {
        const h = harness(); h.apply(true); h.visible(false);
        const error = new Error('document became hidden'); error.name = 'NotAllowedError';
        h.requests[0].reject(error); await flush();
        assert.equal(h.Module.trusscWakeLock.requested, true); assert.equal(h.Module.trusscWakeLock.busy, false);
        assert.equal(h.requests.length, 1); assert.equal(h.listeners.size, 1); assert.equal(h.notices, 0);
        h.visible(true); assert.equal(h.requests.length, 2);
        const s = h.sentinel(); h.requests[1].resolve(s); await flush();
        assert.equal(h.Module.trusscWakeLock.sentinel, s); assert.equal(h.listeners.size, 1);
    });
    await check('hidden generic rejection also preserves recovery', async () => {
        const h = harness(); h.apply(true); h.visible(false);
        h.requests[0].reject(new Error('hidden')); await flush(); h.visible(true);
        assert.equal(h.requests.length, 2); assert.equal(h.listeners.size, 1); assert.equal(h.notices, 0);
    });
    await check('visible rejection waits without spinning and retries on visibility', async () => {
        const h = harness(); h.apply(true);
        const error = new Error('denied while visible'); error.name = 'NotAllowedError';
        h.requests[0].reject(error); await flush();
        assert.equal(h.requests.length, 1); assert.equal(h.listeners.size, 1); assert.equal(h.notices, 0);
        h.visible(false); h.visible(true); assert.equal(h.requests.length, 2);
        h.requests[1].reject(new Error('denied again')); await flush();
        assert.equal(h.requests.length, 2); assert.equal(h.listeners.size, 1); assert.equal(h.notices, 0);
    });
    await check('disable/re-enable after rejection retries and removes only the disabled listener', async () => {
        const h = harness(); h.apply(true); h.requests[0].reject(new Error('denied')); await flush();
        h.apply(false); assert.equal(h.listeners.size, 0); h.visible(true); assert.equal(h.requests.length, 1);
        h.apply(true); assert.equal(h.requests.length, 2); assert.equal(h.listeners.size, 1);
        const s = h.sentinel(); h.requests[1].resolve(s); await flush();
        assert.equal(h.Module.trusscWakeLock.sentinel, s); assert.equal(h.notices, 0);
    });
    await check('hide/show before rejection reconciles to the visible generation', async () => {
        const h = harness(); h.apply(true); h.visible(false); h.visible(true);
        const error = new Error('old visibility check'); error.name = 'NotAllowedError';
        h.requests[0].reject(error); await flush();
        assert.equal(h.requests.length, 2); assert.equal(h.listeners.size, 1); assert.equal(h.notices, 0);
    });
    await check('disable/re-enable before rejection reconciles the new request', async () => {
        const h = harness(); h.apply(true); h.apply(false); h.apply(true);
        h.requests[0].reject(new Error('old request')); await flush();
        assert.equal(h.requests.length, 2); assert.equal(h.listeners.size, 1);
    });
    await check('synchronous request exception is contained and can recover', async () => {
        const h = harness(); const request = h.navigator.wakeLock.request;
        h.navigator.wakeLock.request = () => { throw new Error('denied'); };
        h.apply(true); await flush(); assert.equal(h.notices, 0); assert.equal(h.listeners.size, 1);
        h.navigator.wakeLock.request = request; h.apply(false); h.apply(true);
        assert.equal(h.requests.length, 1);
    });
    await check('rejected visibility re-acquisition preserves later retries', async () => {
        const h = harness(); h.apply(true); const s = h.sentinel(); h.requests[0].resolve(s); await flush();
        s.drop(); h.visible(false); h.visible(true); h.requests[1].reject(new Error('denied')); await flush();
        assert.equal(h.requests.length, 2); h.visible(false); h.visible(true);
        assert.equal(h.requests.length, 3); assert.equal(h.notices, 0); assert.equal(h.listeners.size, 1);
    });
    await check('release rejection is contained and preserves later retries', async () => {
        const h = harness(); h.apply(true); const s = h.sentinel(); h.requests[0].resolve(s); await flush();
        s.release = () => { s.drop(); return Promise.reject(new Error('release failed')); };
        h.apply(false); await flush(); assert.equal(h.listeners.size, 0);
        h.apply(true); assert.equal(h.notices, 0); assert.equal(h.requests.length, 2);
    });
    await check('already released result does not spin', async () => {
        const h = harness(); h.apply(true); const s = h.sentinel(); s.drop();
        h.requests[0].resolve(s); await flush(); assert.equal(h.Module.trusscWakeLock.sentinel, null);
        assert.equal(h.requests.length, 1);
    });
    console.log(`PASS ${count} wake-lock scenarios`);
}
run().catch(error => { console.error(error); process.exitCode = 1; });
