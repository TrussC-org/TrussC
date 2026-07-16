#!/usr/bin/env node
// luagen-types.js — Phase 2: generate tcxLua Sol2 USERTYPE bindings (constructors,
// properties, methods, static methods, operators) from reference-data.json.
//
//   node luagen-types.js <reference-data.json> > trussctype_generated.cpp
//
// Pairs with luagen.js (Phase 1 = free functions). Custom Lua glue (Json/Xml
// get_string etc.) is NOT in reference-data, so those stay hand-written; this only
// emits the structural surface. Correctness is verified by compiling the output.

const fs = require('fs');
const nodePath = require('path');
// argv: <reference-data.json> [colors.json] [--shards N --outdir DIR]
// Without --shards the single file goes to stdout (legacy behavior). With
// --shards N the output is written to DIR as N shard TUs + an aggregator —
// splitting the sol2 template instantiations across TUs keeps peak compiler
// memory low enough for small machines (a single 100-usertype TU cannot be
// compiled on an 8 GB Raspberry Pi 5 even at -j1).
const argv = process.argv.slice(2);
const flags = {};
const positional = [];
for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--shards') flags.shards = parseInt(argv[++i], 10);
    else if (argv[i] === '--outdir') flags.outdir = argv[++i];
    else positional.push(argv[i]);
}
const path = positional[0];
if (!path) { console.error('usage: node luagen-types.js <reference-data.json> [colors.json] [--shards N --outdir DIR]'); process.exit(1); }
if (flags.shards > 1 && !flags.outdir) { console.error('--shards requires --outdir'); process.exit(1); }
const data = JSON.parse(fs.readFileSync(path, 'utf8'));
const colorsPath = positional[1];
const colorsData = colorsPath ? JSON.parse(fs.readFileSync(colorsPath, 'utf8')) : null;

const skip = { template: 0, unbindable: 0, op: 0 };
const report = [];

// ---- shared helpers (same rules as luagen.js) --------------------------
const TRUSSC_TYPES = [];
for (const id in data) { const e = data[id]; if ((e.kind === 'type' || e.kind === 'enum' || e.kind === 'typedef') && !e.owner && !e.ns) TRUSSC_TYPES.push(e.name); }
function qualifyType(t) {
    let out = t;
    for (const name of TRUSSC_TYPES) out = out.replace(new RegExp('(^|[^:\\w])(' + name + ')(?![\\w])', 'g'), '$1trussc::$2');
    return out;
}
const PRIM = new Set(['void','bool','char','short','int','long','float','double','unsigned','signed','size_t','wchar_t','char16_t','char32_t','int8_t','int16_t','int32_t','int64_t','uint8_t','uint16_t','uint32_t','uint64_t']);
function argBindable(a) {
    if (a.isArray || a.isPointer) return false;
    if (/\binternal::/.test(a.type)) return false;   // internal:: types are incomplete / not public API
    if (a.isRef && !a.isConst && !/&&/.test(a.type)) {
        const base = a.type.replace(/[&*]/g, '').replace(/\bconst\b/g, '').trim();
        const allPrim = base.split(/\s+/).every(w => PRIM.has(w));
        const stdValue = base.includes('std::') || /\b(string|basic_string|vector|map|set|pair|tuple|function)\b/.test(base);
        if (allPrim || stdValue) return false;
    }
    return true;
}
// types-only param list (for sol::constructors<T(...)>), qualified
function typeList(args) { return args.map(a => qualifyType(a.type)).join(', '); }
// "decltype-or-value" not needed for members; we use member pointers / simple lambdas.

// ---- operator symbol -> sol::meta_function ------------------------------
// Only operators Lua/sol support. Compound-assign (+=…), !=, =, << are skipped.
const OP_BINARY = { '+': 'addition', '-': 'subtraction', '*': 'multiplication', '/': 'division',
    '%': 'modulus', '==': 'equal_to', '<': 'less_than', '<=': 'less_than_or_equal_to', '[]': 'index' };
const OP_UNARY = { '-': 'unary_minus' };

// substitute a template param (Tween<T>: T->float) inside a type string
function subT(type, T) { return T ? type.replace(/\bT\b/g, T) : type; }

// ---- platform guards -----------------------------------------------------
// A `platforms` annotation (TC_PLATFORMS) restricts where a symbol exists.
// reference-data is generated on macOS, so members compiled out elsewhere
// (e.g. VideoWriter::submitFrame under TC_ASYNC_SCREEN_CAPTURE) are recorded —
// guard their bindings with the matching preprocessor condition.
const ALL_PLATFORMS = ['macos', 'windows', 'linux', 'android', 'ios', 'web'];
const PLATFORM_COND = {
    macos: '(defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE))',
    ios: '(defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)',
    windows: 'defined(_WIN32)',
    linux: '(defined(__linux__) && !defined(__ANDROID__))',
    android: 'defined(__ANDROID__)',
    web: 'defined(__EMSCRIPTEN__)',
};
// -> condition string, or null when unrestricted (no annotation / covers all)
function platformGuard(platforms) {
    if (!platforms || !platforms.length) return null;
    if (ALL_PLATFORMS.every(p => platforms.includes(p))) return null;
    return platforms.map(p => PLATFORM_COND[p]).filter(Boolean).join(' || ');
}

// ---- emit one usertype --------------------------------------------------
// `cppType` = the C++ type to bind (e.g. "Vec2" or "Tween<float>"),
// `luaName` = the Lua-facing name ("Vec2" or "Tween_float"),
// `T` = template arg to substitute (or null).
function emitType(typeEntry, cppType, luaName, T) {
    const owner = typeEntry.id;          // members have owner == this id
    const Q = qualifyType(cppType);      // qualified C++ type for new_usertype<>
    const bare = typeEntry.name.replace(/<.*>/, '');

    // nested types/enums of this owner (e.g. SoundSource::Kind) — qualify bare refs.
    const nested = new Set();
    const oesc = owner.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    for (const id in data) { const m = id.match(new RegExp('^' + oesc + '::([A-Za-z_]\\w*)$')); if (m && (data[id].kind === 'type' || data[id].kind === 'enum')) nested.add(m[1]); }
    function qual(type) {
        let t = qualifyType(subT(type, T));                      // top-level trussc types
        for (const n of nested) t = t.replace(new RegExp('(^|[^:\\w])(' + n + ')(?![\\w])', 'g'), `$1trussc::${owner}::$2`);
        // fallback: a still-bare PascalCase token (not yet qualified, not primitive) is
        // almost certainly a member typedef/nested type of this owner not recorded in
        // reference-data (e.g. Node::Ptr, Font::GlyphVisitor) — qualify as owner-nested.
        t = t.replace(/(^|[^:\w])([A-Z]\w*)(?![\w])/g, (full, pre, w) => PRIM.has(w) ? full : `${pre}trussc::${owner}::${w}`);
        return t;
    }

    const ctorTypes = [...new Set((typeEntry.constructors || [])
        .filter(c => {
            const a = c.args || [];
            if (!a.map(x => ({ ...x, type: subT(x.type, T) })).every(argBindable)) return false;  // unbindable/internal arg
            if (a.length !== 1) return true;
            const b = a[0].type.replace(/[&]/g, '').replace(/\bconst\b/g, '').replace(/<.*>/, '').trim();
            return b !== bare;   // drop copy/move ctor (non-copyable types delete it)
        })
        // expand trailing-default args into one ctor signature per arity, so e.g.
        // Color(float,float,float,float a=1) yields BOTH Color(f,f,f) and Color(f,f,f,f).
        .flatMap(c => {
            const a = c.args || [];
            let fd = a.length;
            a.forEach((x, i) => { if (x.hasDefault && fd === a.length) fd = i; });
            const out = [];
            for (let k = fd; k <= a.length; k++) out.push(`${Q}(${a.slice(0, k).map(x => qual(x.type)).join(', ')})`);
            return out;
        }))];

    // lambdas for a member fn with trailing-default arity expansion (Lua optional args)
    function memberLambdas(name, sigs, isStatic) {
        const lams = [];
        for (const sig of sigs) {
            const refRet = /&\s*$/.test(sig.ret || '');   // preserve reference (non-copyable / chaining) returns
            const args = (sig.args || []).map((a, i) => ({ type: qual(a.type), nm: a.name || `a${i}`, hasDefault: a.hasDefault }));
            let firstDefault = args.length;
            args.forEach((a, i) => { if (a.hasDefault && firstDefault === args.length) firstDefault = i; });
            for (let k = firstDefault; k <= args.length; k++) {
                const slice = args.slice(0, k);
                const decl = (isStatic ? [] : [`${Q}& self`]).concat(slice.map(a => `${a.type} ${a.nm}`)).join(', ');
                const call = `${isStatic ? `${Q}::${name}` : `self.${name}`}(${slice.map(a => a.nm).join(', ')})`;
                lams.push(`[](${decl})${refRet ? ' -> decltype(auto)' : ''} { return ${call}; }`);
            }
        }
        return [...new Set(lams)];
    }

    // collect members owned by this type
    const ops = {};        // meta_function -> [lambda,...]
    const props = [], methods = {}, statics = {};
    for (const id in data) {
        const e = data[id];
        if (e.owner !== owner) continue;
        if (e.access && e.access !== 'public') { skip.nonpublic = (skip.nonpublic || 0) + 1; continue; }  // protected/private
        if (e.tparams && e.tparams.length) { skip.template++; continue; }
        const opm = e.name.match(/^operator(.+)$/);
        if (opm) {
            if (platformGuard(e.platforms)) { skip.platform = (skip.platform || 0) + 1; continue; }  // rare; keep the ctor/op expression unguarded
            const sym = opm[1];
            for (const sig of (e.signatures || [])) {   // an operator can have unary + binary overloads
                if (sig.tmpl) continue;
                const args = (sig.args || []).map(a => ({ ...a, type: subT(a.type, T) }));
                if (!args.every(argBindable)) { skip.unbindable++; continue; }
                let meta, lam;
                if (args.length === 0 && OP_UNARY[sym]) { meta = OP_UNARY[sym]; lam = `[](const ${Q}& a){ return -a; }`; }
                else if (args.length === 1 && OP_BINARY[sym]) {
                    meta = OP_BINARY[sym];
                    const at = qual(args[0].type);
                    const expr = sym === '[]' ? `a[b]` : `a ${sym} b`;
                    lam = `[](const ${Q}& a, ${at} b){ return ${expr}; }`;
                } else { skip.op++; continue; }   // unsupported (+=, <<, =, !=, …)
                (ops[meta] ||= []).push(lam);
            }
            continue;
        }
        if (e.kind === 'field') {
            const ft = subT(e.type || '', T);
            // skip fields of incomplete internal:: types, raw pointers, or C arrays
            if (!ft || /\binternal::/.test(ft) || /\*/.test(ft) || /\[/.test(ft)) { skip.unbindable++; continue; }
            props.push({ name: e.name, guard: platformGuard(e.platforms) });
            continue;
        }
        if (e.kind === 'method') {
            const sigs = (e.signatures || []).filter(s => !s.tmpl);
            if (!sigs.length) continue;
            // bindability: every overload's args must be bindable to use a member ptr;
            // skip individual unbindable overloads when emitting lambdas.
            const usable = sigs.filter(s => !/\binternal::/.test(s.ret || '')   // skip internal:: (incomplete) returns
                && (s.args || []).map(a => ({ ...a, type: subT(a.type, T) })).every(argBindable));
            if (!usable.length) { skip.unbindable++; continue; }
            const target = e.static ? statics : methods;
            target[e.name] = { sigs: usable, total: sigs.length, guard: platformGuard(e.platforms) };
        }
    }

    let s = `    {\n        sol::usertype<${Q}> t = lua->new_usertype<${Q}>("${luaName}"`;
    // Positional constructors provide Type.new(...); sol::call_constructor
    // additionally enables the Type(...) call form (without it the usertype
    // table has no __call and e.g. `Vec3(1, 2, 3)` fails with "attempt to
    // call a table value").
    if (ctorTypes.length) {
        s += `,\n            sol::constructors<${ctorTypes.join(', ')}>()`;
        s += `,\n            sol::call_constructor, sol::constructors<${ctorTypes.join(', ')}>()`;
    }
    for (const meta in ops) {
        const lams = [...new Set(ops[meta])];
        s += `,\n            sol::meta_function::${meta}, ` + (lams.length === 1 ? lams[0] : `sol::overload(${lams.join(', ')})`);
    }
    s += `);\n`;
    const seenProps = new Set();
    for (const p of props) {
        if (seenProps.has(p.name)) continue;
        seenProps.add(p.name);
        const line = `        t["${p.name}"] = &${Q}::${p.name};\n`;
        s += p.guard ? `#if ${p.guard}\n${line}#endif\n` : line;
    }
    const emitFns = (map, isStatic) => {
        for (const name in map) {
            const { sigs, total, guard } = map[name];
            const hasDefaults = sigs.some(sg => (sg.args || []).some(a => a.hasDefault));
            let line;
            if (total === 1 && !hasDefaults) {
                line = `        t["${name}"] = &${Q}::${name};\n`;   // single C++ overload, no defaults -> member ptr (clean)
            } else {
                const lams = memberLambdas(name, sigs, isStatic);  // defaults -> arity overloads
                line = `        t["${name}"] = ${lams.length === 1 ? lams[0] : `sol::overload(${lams.join(', ')})`};\n`;
            }
            s += guard ? `#if ${guard}\n${line}#endif\n` : line;
        }
    };
    emitFns(methods, false);
    emitFns(statics, true);
    s += `    }\n`;
    return s;
}

// ---- drive over all types ----------------------------------------------
// Types not generated structurally:
//  - Json/Xml: custom Lua glue (get_string/table index) not in reference-data.
//  - Shader/Thread: have protected members that reference-data does NOT flag
//    (no access modifier) -> generator can't filter them. Needs reference-data
//    to mark access; until then, hand-written. [report -> 本体側]
//  - SoundSource: references nested type SoundSource::Kind (generator only
//    qualifies top-level types yet).
//  - FileWriter/FileReader: non-copyable (deleted copy ctor / operator=).
const EXCLUDE = new Set([
    'Json', 'Xml',   // custom Lua glue (get_string / table index) not in reference-data
    // These 4 embed/inherit an incomplete internal:: type (internal::StreamInstance) in
    // their C++ definition — not visible in reference-data (no private members/bases), so
    // `new_usertype<T>` fails to instantiate sol's type traits on the incomplete type.
    // (Serial/Node were earlier mis-excluded via a clang cascade; they compile fine.)
    'Thread', 'SoundSource', 'Environment', 'VideoPlayerBase',
    // --- still hand-written: these have custom members the generator can't produce
    // (raw-pointer/out-param marshalling, Lua-keyword renames like end_fbo, texture
    // setters). v2 migrated the 14 gen⊇hand types (Vec2/Color/Mesh/… now generated).
    'AudioEngine', 'EasyCam', 'Fbo', 'Image', 'Light', 'Mat3', 'Mat4', 'Material',
    'MicInput', 'Pixels', 'Shader', 'SoundBuffer', 'Texture',
]);

// Heavy blocks (usertypes + enums) are collected individually so they can be
// bin-packed across shard TUs; light tail bindings (constants, colors) stay in
// the aggregator. `body` remains the concatenation for single-file output.
const blocks = [];
let body = '', count = 0;
// wrap a whole usertype block when the TYPE itself is platform-restricted
function guardedType(code, e) {
    const g = platformGuard(e.platforms);
    return g ? `#if ${g}\n${code}#endif\n` : code;
}
for (const id in data) {
    const e = data[id];
    if (e.kind !== 'type' || e.owner || e.ns) continue;
    if (EXCLUDE.has(e.name)) { report.push(`${e.name}: excluded (custom Lua glue, hand-written)`); continue; }
    if (e.tparams && e.tparams.length) {
        // templated type: instantiate per lua_bind entry, else report
        if (!e.lua_bind || !e.lua_bind.length) { report.push(`${e.name}: templated, no lua_bind (skipped)`); continue; }
        for (const T of e.lua_bind) {
            const base = e.name.replace(/<.*>/, '');
            try { blocks.push(guardedType(emitType(e, `${base}<${T}>`, `${base}_${T.replace(/[^A-Za-z0-9]/g, '')}`, T), e)); count++; }
            catch (err) { report.push(`${e.name}<${T}>: ${err.message}`); }
        }
        continue;
    }
    try { blocks.push(guardedType(emitType(e, e.name, e.name, null), e)); count++; }
    catch (err) { report.push(`${e.name}: ${err.message}`); }
}

// ---- enums ----------------------------------------------------------------
// Same style as the previous hand bindings (usertype + equal_to + sol::var per
// value) so semantics don't change: values are usertype instances, == works via
// the metamethod, and C++ enum returns compare equal to the constants.
// Enums are constants declared on every platform -> no platform guards.
let enumCount = 0, nestedEnums = [];
for (const id in data) {
    const e = data[id];
    if (e.kind !== 'enum' || e.ns) continue;
    if (e.owner) { nestedEnums.push(id); continue; }   // nested (SoundSource::Kind) — future
    if (!e.members || !e.members.length) { report.push(`${e.name}: enum without members (skipped)`); continue; }
    const Q = `trussc::${e.name}`;
    let s2 = `    lua->new_usertype<${Q}>("${e.name}",\n`;
    s2 += `        sol::meta_function::equal_to, [](${Q} a, ${Q} b){ return a == b; }`;
    for (const m of e.members) s2 += `,\n        "${m.name}", sol::var(${Q}::${m.name})`;
    s2 += `);\n`;
    blocks.push(s2); enumCount++;
}
if (nestedEnums.length) report.push(`nested enums (not yet emitted): ${nestedEnums.join(', ')}`);

// ---- constants (kind:var) --------------------------------------------------
// Top-level non-hidden constants (TAU, KEY_*, MOUSE_BUTTON_*, VSYNC, Direction
// shorthands Left/Center/...). Plain assignments; sol converts values as usual.
let constCount = 0;
let tail = '';   // light bindings kept in the aggregator TU (constants, colors)
tail += `    // constants\n`;
for (const id in data) {
    const e = data[id];
    if (e.kind !== 'var' || e.owner || e.ns || e.hidden) continue;
    tail += `    (*lua)["${e.name}"] = trussc::${e.name};\n`;
    constCount++;
}

// ---- colors ---------------------------------------------------------------
// colors.json is the canonical color list (docs/reference/). Emit the same
// `colors` table the hand binding used: colors.white etc. as sol::var constants.
let colorCount = 0;
if (colorsData) {
    // `colors` is just a named table; use a file-local tag struct (emitted in the
    // prelude below) as the usertype key — there is no trussc::Colors.
    tail += `    {\n        sol::usertype<TcxLuaColorsTable> t = lua->new_usertype<TcxLuaColorsTable>("colors");\n`;
    for (const group of colorsData) for (const item of group.items) {
        tail += `        t["${item.name}"] = sol::var(trussc::colors::${item.name});\n`;
        colorCount++;
    }
    tail += `    }\n`;
}

const PRELUDE = (needsColorsTag) => `// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
${needsColorsTag ? 'namespace { struct TcxLuaColorsTable {}; }   // tag for the `colors` constant table\n' : ''}#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
`;
const POSTLUDE = `#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
`;

const shards = flags.shards > 1 ? flags.shards : 1;
if (shards === 1) {
    // Legacy single-file output on stdout.
    body = blocks.join('') + tail;
    process.stdout.write(`${PRELUDE(true)}void tcxLua::setGeneratedTypeBindings(const std::shared_ptr<sol::state>& lua) {
${body}}
${POSTLUDE}`);
} else {
    // Shard output: greedy bin-packing of the heavy blocks (block size is a
    // fair proxy for sol2 template load) into N TUs, plus an aggregator that
    // defines setGeneratedTypeBindings, calls each shard, and carries the
    // light tail (constants + colors).
    const buckets = Array.from({ length: shards }, () => ({ size: 0, parts: [] }));
    for (const b of [...blocks].sort((a, c) => c.length - a.length)) {
        const min = buckets.reduce((x, y) => (y.size < x.size ? y : x));
        min.parts.push(b); min.size += b.length;
    }
    const outdir = flags.outdir;
    // Remove stale shard files from a previous (different-N) run.
    for (const f of fs.readdirSync(outdir)) {
        if (/^trussctype_generated_\d+\.cpp$/.test(f)) fs.unlinkSync(nodePath.join(outdir, f));
    }
    const decls = [];
    buckets.forEach((bk, i) => {
        const nn = String(i).padStart(2, '0');
        const fn = `tcxLuaGenShard_${nn}`;
        decls.push(fn);
        fs.writeFileSync(nodePath.join(outdir, `trussctype_generated_${nn}.cpp`),
            `${PRELUDE(false)}void ${fn}(const std::shared_ptr<sol::state>& lua) {
${bk.parts.join('')}}
${POSTLUDE}`);
    });
    fs.writeFileSync(nodePath.join(outdir, 'trussctype_generated.cpp'),
        `${PRELUDE(true)}${decls.map((d) => `void ${d}(const std::shared_ptr<sol::state>& lua);`).join('\n')}
void tcxLua::setGeneratedTypeBindings(const std::shared_ptr<sol::state>& lua) {
${decls.map((d) => `    ${d}(lua);`).join('\n')}
${tail}}
${POSTLUDE}`);
    console.error(`[luagen-types] wrote ${shards} shard TUs + aggregator to ${outdir} (largest shard ${Math.max(...buckets.map(b => b.size))} bytes)`);
}
console.error(`[luagen-types] usertypes: ${count} | enums: ${enumCount} | colors: ${colorCount} | consts: ${constCount} | skipped members: template ${skip.template}, unbindable ${skip.unbindable}, unsupported-op ${skip.op}`);
for (const r of report) console.error('  ' + r);
