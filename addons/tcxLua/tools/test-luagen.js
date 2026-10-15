#!/usr/bin/env node
// Regression checks of the resolved-list contract, with no generated files.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { createRequire } = require('node:module');

const data = {
    Derived: { id: 'Derived', kind: 'type', name: 'Derived', platforms: ['linux'],
        constructors: [{ args: [], platforms: ['windows'] },
            { args: [{ type: 'int', name: 'value' }], platforms: ['macos'] }] },
    'Derived::inherited': { kind: 'method', name: 'inherited', owner: 'Derived',
        declaredIn: 'Base', platforms: ['web'], signatures: [{ ret: 'void', args: [] }] },
    'Derived::value': { kind: 'field', name: 'value', owner: 'Derived', type: 'int', platforms: ['ios'] },
    'Derived::operator-': { kind: 'method', name: 'operator-', owner: 'Derived',
        platforms: ['windows'], signatures: [{ ret: 'Derived', args: [] },
            { ret: 'Derived', args: [{ type: 'const Derived &', isRef: true, isConst: true }] }] },
    Base: { id: 'Base', kind: 'type', name: 'Base' },
    'Base::Result': { id: 'Base::Result', kind: 'type', name: 'Result', owner: 'Base' },
    'Derived::Result': { id: 'Derived::Result', kind: 'type', name: 'Result', owner: 'Derived', declaredIn: 'Base' },
    'Base::Result::hit': { kind: 'method', name: 'hit', owner: 'Base::Result', signatures: [{ ret: 'bool', args: [] }] },
    'Base::Result::distance': { kind: 'field', name: 'distance', owner: 'Base::Result', type: 'float' },
    VideoPlayerBase: { id: 'VideoPlayerBase', kind: 'type', name: 'VideoPlayerBase' },
    guarded: { kind: 'func', name: 'guarded', platforms: ['android'], signatures: [{ ret: 'void', args: [] }] },
    unrestricted: { kind: 'func', name: 'unrestricted', signatures: [{ ret: 'void', args: [] }] },
};
function generate(script) {
    const files = new Map(); let stdout = '';
    const filename = path.join(__dirname, script);
    vm.runInNewContext(fs.readFileSync(filename, 'utf8'), {
        require: name => name === 'fs' ? {
            readFileSync: file => file === 'input' ? JSON.stringify(data) : fs.readFileSync(file, 'utf8'),
            readdirSync: () => [],
            writeFileSync: (file, content) => files.set(file, content),
        } : createRequire(filename)(name),
        __dirname: path.dirname(filename),
        process: { argv: ['node', filename, 'input'], stdout: { write: s => { stdout += s; } },
            exit: code => { throw new Error(`generator exited ${code}`); } },
        console: { error: () => {} },
    }, { filename });
    return stdout + [...files.values()].join('\n');
}
const types = generate('luagen-types/luagen-types.js');
assert.match(types, /#if defined\(_WIN32\)\n\s*, trussc::Derived\(\)\n#endif/);
assert.match(types, /#if .*__APPLE__.*\n\s*, trussc::Derived\(int\)\n#endif/);
assert.match(types, /#if defined\(__EMSCRIPTEN__\)\n\s*t\["inherited"\] = &trussc::Derived::inherited;\n#endif/);
assert.match(types, /#if .*TARGET_OS_IPHONE.*\n\s*t\["value"\] = &trussc::Derived::value;\n#endif/);
for (const meta of ['unary_minus', 'subtraction']) {
    assert.match(types, new RegExp(`#if defined\\(_WIN32\\)\\n\\s*t\\[sol::meta_function::${meta}\\] = .*\\n#endif`));
}
assert.match(types, /new_usertype<trussc::VideoPlayerBase>/);
assert.match(types, /new_usertype<trussc::Base::Result>\("BaseResult"/);
assert.match(types, /t\["hit"\] = &trussc::Base::Result::hit;/);
assert.match(types, /t\["distance"\] = &trussc::Base::Result::distance;/);
assert.doesNotMatch(types, /DerivedResult|new_usertype<[^>]*Derived::Result>/);
assert.doesNotMatch(types, /registry\(\)|registered\.as|\(\*lua\)\["Derived"\] = t;/);
const funcs = generate('luagen/luagen.js');
assert.match(funcs, /#if defined\(__ANDROID__\)\n\s*lua->set_function\("guarded".*\n#endif/);
assert.match(funcs, /#endif\n\s*lua->set_function\("unrestricted"/);
console.log('Lua generators: resolved entry guards, inherited members, inherited TYPE omission and base inclusion PASS');
let names = '';
const done = new Error('names filtered');
try {
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../bindcheck/classify_members.js'), 'utf8'), {
        require: () => ({
            readFileSync: file => file === 0 ? 'guarded\nunrestricted\nDerived\nVideoPlayerBase\n' : JSON.stringify(data),
        }),
        process: { argv: ['node', 'classify_members.js', 'input', '--names'], platform: 'linux',
            stdout: { write: s => { names += s; } }, exit: () => { throw done; } },
    });
} catch (error) { if (error !== done) throw error; }
assert.equal(names, 'unrestricted\nDerived\nVideoPlayerBase\n');
console.log('bindcheck: expected names follow resolved platforms PASS');
