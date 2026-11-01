// Run with CXX=clang++ node --test docs/reference/tests/resolved-list.test.js.
// Uses a tiny real clang AST, without a TrussC build or generated doc changes.
const assert = require('node:assert/strict');
const { test, after } = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const scripts = path.resolve(__dirname, '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'tc-resolved-list-'));
after(() => fs.rmSync(tmp, { recursive: true, force: true }));
const header = path.join(tmp, 'core/include/tc/resolved.h');
fs.mkdirSync(path.dirname(header), { recursive: true });
fs.writeFileSync(header, `
#define TC_PLATFORMS(s) [[clang::annotate("tc:platforms:" s)]]
namespace trussc {
struct HasTexture {
    HasTexture();
    struct NestedBase { NestedBase(); };
    bool hasTexture() const;
    int getTexture();
    int getTexture() const;
    int value;
    void hiddenByDerived();
protected:
    void protectedMethod();
private:
    void privateMethod();
};
struct Mid : public HasTexture {};
struct TC_PLATFORMS("linux") Fbo : public Mid {
    Fbo();
    TC_PLATFORMS("web") Fbo(int count);
    [[deprecated("old ctor")]] Fbo(float count);
    TC_PLATFORMS("web") void ownPlatform();
    TC_PLATFORMS("web") int fieldPlatform;
    int ownField;
    void getTexture(int count);
    [[deprecated("old overload")]] void mixed(int count);
    void mixed(float count);
    struct Nested { int field; };
private:
    void hiddenByDerived();
};
struct [[deprecated("old type")]] TC_PLATFORMS("linux") Obsolete {
    Obsolete();
    void method();
    int field;
};
class PrivateBase : HasTexture {};
struct ProtectedBase : protected HasTexture {};
TC_PLATFORMS("web") void freeFunction();
template <typename T> [[deprecated("old template")]] void templateOld(T value);
namespace internal { void hiddenFunction(); }
namespace mcp { void publicFunction(); }
}
`);
const ast = path.join(tmp, 'fixture.ast');
fs.writeFileSync(ast, execFileSync(process.env.CXX || 'clang++', [
    '-x', 'c++', '-std=c++20', '-Wno-deprecated-declarations', '-Xclang', '-ast-dump=json',
    '-Xclang', '-ast-dump-filter=trussc', '-fsyntax-only', header,
]));
function run(script, args = []) {
    return execFileSync(process.execPath, [path.join(scripts, script), ...args], { encoding: 'utf8' });
}
const structureFile = path.join(tmp, 'structure.json');
run('structure.js', ['--ast', ast, '--json', structureFile]);
const data = JSON.parse(fs.readFileSync(structureFile, 'utf8'));

test('public macros include object-like definitions and remain non-callable', () => {
    const macros = Object.values(data).filter(e => e.kind === 'macro');
    assert.equal(macros.length, 11);
    assert.ok(data.TC_RUN_APP);
    assert.ok(data.TC_LUA_SKIP); // no parentheses in this macro's #define
    for (const entry of macros) {
        assert.equal(entry.platforms, null, entry.id);
        assert.equal(entry.deprecated, null, entry.id);
        assert.equal(entry.lua_skip, true, entry.id);
        assert.deepEqual(entry.signatures, [], entry.id);
    }
});

test('annotations resolve from declaration or owner for all entry kinds', () => {
    assert.deepEqual(data.Fbo.platforms, ['linux']);
    assert.deepEqual(data.Fbo.constructors.map(s => s.platforms), [['linux'], ['web'], ['linux']]);
    assert.ok(data.Fbo.constructors[2].deprecated);
    assert.deepEqual(data['Fbo::ownPlatform'].platforms, ['web']);
    assert.deepEqual(data['Fbo::fieldPlatform'].platforms, ['web']);
    assert.deepEqual(data['Fbo::ownField'].platforms, ['linux']);
    assert.deepEqual(data['Fbo::Nested::field'].platforms, ['linux']);
    assert.deepEqual(data.freeFunction.platforms, ['web']);
    assert.ok(data.templateOld.deprecated);
    for (const entry of Object.values(data)) {
        assert.ok(Object.hasOwn(entry, 'platforms'), entry.id);
        assert.ok(Object.hasOwn(entry, 'deprecated'), entry.id);
    }
    for (const id of ['Obsolete', 'Obsolete::method', 'Obsolete::field']) assert.ok(data[id].deprecated, id);
    assert.ok(data.Obsolete.constructors[0].deprecated);
    assert.equal(data['Fbo::mixed'].deprecated, null);
    assert.ok(data['Fbo::mixed'].signatures[0].deprecated);
    assert.equal(data['Fbo::mixed'].signatures[1].deprecated, null);
});

test('public inheritance walks to the root and preserves base entries and overloads', () => {
    assert.equal(data['Fbo::hasTexture'].declaredIn, 'HasTexture');
    assert.equal(data['Fbo::value'].declaredIn, 'HasTexture');
    assert.deepEqual(data['Fbo::hasTexture'].platforms, ['linux']);
    assert.equal(data['HasTexture::hasTexture'].declaredIn, undefined);
    assert.equal(data['HasTexture::getTexture'].signatures.length, 2);
    assert.equal(data['Mid::getTexture'].signatures.length, 2);
    assert.equal(data['Fbo::getTexture'].signatures.length, 1); // derived name hides the base name
    assert.equal(data['Fbo::getTexture'].declaredIn, undefined);
    assert.equal(data['Fbo::protectedMethod'], undefined);
    assert.equal(data['Fbo::privateMethod'], undefined);
    assert.equal(data['Fbo::hiddenByDerived'], undefined);
    assert.equal(data['PrivateBase::hasTexture'], undefined);
    assert.equal(data['ProtectedBase::hasTexture'], undefined);
    assert.equal(data.Mid.constructors, undefined);
    assert.equal(data['HasTexture::NestedBase'].constructors[0].platforms, null);
    assert.deepEqual(data['Fbo::NestedBase'].constructors[0].platforms, ['linux']);
});

test('the hidden set and documentation ids remain authoritative', () => {
    assert.equal(data['internal::hiddenFunction'], undefined);
    assert.ok(data['mcp::publicFunction']);
    const ids = run('structure.js', ['--ast', ast, '--ids']).trim().split('\n');
    assert.ok(ids.includes('HasTexture::hasTexture'));
    assert.ok(!ids.includes('Fbo::hasTexture'));
});

test('the join preserves inherited metadata and prose for coverage, but docs omit copies', () => {
    const joinedFile = path.join(tmp, 'joined.json');
    run('generate.js', ['--structure', structureFile, '--out-json', joinedFile, '--out-md', path.join(tmp, 'index.md')]);
    const joined = JSON.parse(fs.readFileSync(joinedFile, 'utf8'));
    assert.equal(joined['Fbo::hasTexture'].declaredIn, 'HasTexture');
    assert.equal(joined['Fbo::hasTexture'].documented, true);
    assert.deepEqual(joined['Fbo::hasTexture'].description, joined['HasTexture::hasTexture'].description);
    assert.equal(joined.TC_LUA_SKIP.signature, 'TC_LUA_SKIP <decl>');
    assert.equal(joined.TC_NO_HOT_RELOAD.signature, 'TC_NO_HOT_RELOAD("reason");');
    assert.equal(joined.TC_LUA_SKIP.lua_skip, true);
    assert.ok(run('emit-forai.js', ['--data', joinedFile, '--dry']).includes('TC_LUA_SKIP <decl>  //'));
    const withoutCopies = Object.fromEntries(Object.entries(joined).filter(([, e]) => !e.declaredIn));
    const ownFile = path.join(tmp, 'own.json');
    fs.writeFileSync(ownFile, JSON.stringify(withoutCopies));
    for (const emitter of ['emit-forai.js', 'emit-of.js']) {
        assert.equal(run(emitter, ['--data', joinedFile, '--dry']), run(emitter, ['--data', ownFile, '--dry']));
    }
});

test('coverage uses entry guards, skips deprecation, and calls const overloads on const owners', () => {
    // Treat fixture declarations as documented without adding test-only TOML prose.
    const documented = Object.fromEntries(Object.entries(data).map(([id, e]) => [id, { ...e, documented: true }]));
    const file = path.join(tmp, 'documented.json');
    const cpp = path.join(tmp, 'coverage.cpp');
    fs.writeFileSync(file, JSON.stringify(documented));
    run('emit-coverage.js', ['--data', file, '--out', cpp]);
    const output = fs.readFileSync(cpp, 'utf8');
    assert.match(output, /af::val<HasTexture>\(\)\.getTexture\(\)/);
    assert.match(output, /af::val<const HasTexture>\(\)\.getTexture\(\)/);
    assert.match(output, /#if defined\(AF_LINUX\)\n\s*\(void\)af::val<const Fbo>\(\)\.hasTexture\(\);\n#endif/);
    assert.match(output, /#if defined\(AF_WEB\)\n\s*\(void\)Fbo\(af::val<int>\(\)\);\n#endif/);
    assert.match(output, /#if defined\(AF_WEB\)\n\s*\(void\)af::val<Fbo>\(\)\.fieldPlatform;/);
    assert.doesNotMatch(output, /Cover_Obsolete|Fbo\(af::val<float>|mixed\(af::val<int>/);
    assert.match(output, /mixed\(af::val<float>\(\)\)/);
    for (const entry of Object.values(data).filter(e => e.kind === 'macro')) {
        assert.ok(!output.includes(entry.name), entry.id);
    }
});
