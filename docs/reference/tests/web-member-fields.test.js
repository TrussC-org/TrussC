const assert = require('node:assert/strict');
const { test } = require('node:test');
const fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const file = path.resolve(__dirname, '../../scripts/emit-web.js');
const realRequire = require('node:module').createRequire(file);
function emit(ref, yaml = '{}') {
    const ctx = {
        require: name => name === 'fs' ? { ...fs,
            existsSync: f => f.endsWith('api-definition.yaml') || fs.existsSync(f),
            readFileSync: (f, ...args) => f.endsWith('reference-data.json') ? JSON.stringify(ref)
                : f.endsWith('api-definition.yaml') ? yaml : fs.readFileSync(f, ...args),
        } : name === 'child_process' ? { execSync: () => 'fixture' } : realRequire(name),
        __dirname: path.dirname(file), process: { argv: ['node', file] }, console,
    };
    vm.runInNewContext(fs.readFileSync(file, 'utf8').replace(/main\(\);\s*$/, 'globalThis.output = build({});'), ctx);
    return JSON.parse(JSON.stringify(ctx.output));
}
const fields = { details: { en: 'English', ja: '日本語', ko: '한국어' }, related: ['Type'], keywords: ['lookup'], snippet: 'use();', platform_note: { en: 'platform', ja: '環境', ko: '환경' } };
const symbol = (id, kind, extra = {}) => ({ id, name: id.split('::').pop(), kind, signatures: [{ ret: 'void', params: '' }], ...extra });
test('authored fields survive functions, constants, types, instance/static/nested methods', () => {
    const ref = Object.fromEntries([
        symbol('Type', 'type', { constructors: [{ params: '' }] }), symbol('Type::method', 'method', { owner: 'Type' }),
        symbol('Type::staticMethod', 'method', { owner: 'Type', static: true }),
        symbol('Type::Nested', 'type', { owner: 'Type' }), symbol('Type::Nested::method', 'method', { owner: 'Type::Nested' }),
        symbol('function', 'func'), symbol('CONSTANT', 'var'),
    ].map(s => [s.id, { ...s, ...fields }]));
    const api = emit(ref), type = api.types[0];
    for (const e of [type, ...type.methods, ...type.static_methods, type.nested[0], ...type.nested[0].methods, api.categories[0].functions[0], api.constants[0]]) {
        assert.equal(e.details, 'English'); assert.equal(e.details_ja, '日本語'); assert.equal(e.details_ko, '한국어');
        assert.deepEqual(e.related, ['Type']); assert.deepEqual(e.keywords, ['lookup']);
        assert.equal(e.snippet, 'use();'); assert.equal(e.platformNote_ja, '環境');
    }
});
test('plain details and canonical snippet win; old constructor snippet is a fallback', () => {
    const ref = { Type: symbol('Type', 'type', { details: 'Plain details', snippet: 'canonical();', constructors: [{ params: '' }] }) };
    const yaml = 'types:\n  - name: Type\n    constructor:\n      snippet: legacy();\n';
    const type = emit(ref, yaml).types[0];
    assert.equal(type.details, 'Plain details'); assert.equal(type.snippet, 'canonical();');
    delete ref.Type.snippet;
    assert.equal(emit(ref, yaml).types[0].snippet, 'legacy();');
});
test('unannotated members gain no empty authored fields', () => {
    const api = emit({ Type: symbol('Type', 'type'), 'Type::method': symbol('Type::method', 'method', { owner: 'Type' }) });
    assert.deepEqual(api.types[0].methods[0], { name: 'method', signatures: [{ ret: 'void', params: '' }], desc: '' });
});
test('members copied from a base carry the declaring type; own members do not', () => {
    const api = emit({
        Base: symbol('Base', 'type'), Derived: symbol('Derived', 'type'),
        'Base::shared': symbol('Base::shared', 'method', { owner: 'Base' }),
        'Derived::shared': symbol('Derived::shared', 'method', { owner: 'Derived', declaredIn: 'Base' }),
        'Derived::own': symbol('Derived::own', 'method', { owner: 'Derived' }),
    });
    const methods = (name) => Object.fromEntries(api.types.find(t => t.name === name).methods.map(m => [m.name, m]));
    assert.equal(methods('Derived').shared.inherited, 'Base');
    assert.equal(methods('Derived').own.inherited, undefined);
    assert.equal(methods('Base').shared.inherited, undefined);
});
