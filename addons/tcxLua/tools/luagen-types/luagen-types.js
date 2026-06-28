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
const path = process.argv[2];
if (!path) { console.error('usage: node luagen-types.js <reference-data.json>'); process.exit(1); }
const data = JSON.parse(fs.readFileSync(path, 'utf8'));

const skip = { template: 0, unbindable: 0, op: 0 };
const report = [];

// ---- shared helpers (same rules as luagen.js) --------------------------
const TRUSSC_TYPES = [];
for (const id in data) { const e = data[id]; if ((e.kind === 'type' || e.kind === 'enum') && !e.owner && !e.ns) TRUSSC_TYPES.push(e.name); }
function qualifyType(t) {
    let out = t;
    for (const name of TRUSSC_TYPES) out = out.replace(new RegExp('(^|[^:\\w])(' + name + ')(?![\\w])', 'g'), '$1trussc::$2');
    return out;
}
const PRIM = new Set(['void','bool','char','short','int','long','float','double','unsigned','signed','size_t','wchar_t','char16_t','char32_t','int8_t','int16_t','int32_t','int64_t','uint8_t','uint16_t','uint32_t','uint64_t']);
function argBindable(a) {
    if (a.isArray || a.isPointer) return false;
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

// ---- emit one usertype --------------------------------------------------
// `cppType` = the C++ type to bind (e.g. "Vec2" or "Tween<float>"),
// `luaName` = the Lua-facing name ("Vec2" or "Tween_float"),
// `T` = template arg to substitute (or null).
function emitType(typeEntry, cppType, luaName, T) {
    const owner = typeEntry.id;          // members have owner == this id
    const Q = qualifyType(cppType);      // qualified C++ type for new_usertype<>
    const ctorTypes = (typeEntry.constructors || [])
        .map(c => `${Q}(${typeList((c.args || []).map(a => ({ ...a, type: subT(a.type, T) })))})`);

    // collect members owned by this type
    const ops = {};        // meta_function -> [lambda,...]
    const props = [], methods = {}, statics = {};
    for (const id in data) {
        const e = data[id];
        if (e.owner !== owner) continue;
        if (e.tparams && e.tparams.length) { skip.template++; continue; }
        const opm = e.name.match(/^operator(.+)$/);
        if (opm) {
            const sig = (e.signatures || [])[0] || { args: [] };
            const args = (sig.args || []).map(a => ({ ...a, type: subT(a.type, T) }));
            if (!args.every(argBindable)) { skip.unbindable++; continue; }
            const sym = opm[1];
            let meta, lam;
            if (args.length === 0 && OP_UNARY[sym]) { meta = OP_UNARY[sym]; lam = `[](const ${Q}& a){ return -a; }`; }
            else if (args.length === 1 && OP_BINARY[sym]) {
                meta = OP_BINARY[sym];
                const at = qualifyType(args[0].type);
                const expr = sym === '[]' ? `a[b]` : `a ${sym} b`;
                lam = `[](${/&/.test(at) ? `const ${Q}&` : `const ${Q}&`} a, ${at} b){ return ${expr}; }`;
            } else { skip.op++; continue; }   // unsupported (+=, <<, =, !=, …)
            (ops[meta] ||= []).push(lam);
            continue;
        }
        if (e.kind === 'field') { props.push(e.name); continue; }
        if (e.kind === 'method') {
            const sigs = (e.signatures || []).filter(s => !s.tmpl);
            if (!sigs.length) continue;
            // bindability: every overload's args must be bindable to use a member ptr;
            // skip individual unbindable overloads when emitting lambdas.
            const usable = sigs.filter(s => (s.args || []).map(a => ({ ...a, type: subT(a.type, T) })).every(argBindable));
            if (!usable.length) { skip.unbindable++; continue; }
            const target = e.static ? statics : methods;
            target[e.name] = { sigs: usable, ret: subT((usable[0] || {}).ret || 'auto', T) };
        }
    }

    let s = `    {\n        sol::usertype<${Q}> t = lua->new_usertype<${Q}>("${luaName}"`;
    if (ctorTypes.length) s += `,\n            sol::constructors<${ctorTypes.join(', ')}>()`;
    for (const meta in ops) {
        const lams = [...new Set(ops[meta])];
        s += `,\n            sol::meta_function::${meta}, ` + (lams.length === 1 ? lams[0] : `sol::overload(${lams.join(', ')})`);
    }
    s += `);\n`;
    for (const p of [...new Set(props)]) s += `        t["${p}"] = &${Q}::${p};\n`;
    const emitFns = (map, isStatic) => {
        for (const name in map) {
            const { sigs } = map[name];
            if (sigs.length === 1 && sigs[0].args.every(a => true)) {
                // single overload -> member pointer (clean)
                s += `        t["${name}"] = &${Q}::${name};\n`;
            } else {
                const lams = sigs.map(sig => {
                    const args = (sig.args || []).map((a, i) => ({ t: qualifyType(subT(a.type, T)), n: a.name || `a${i}` }));
                    const decl = isStatic ? args.map(a => `${a.t} ${a.n}`).join(', ')
                        : [`${Q}& self`, ...args.map(a => `${a.t} ${a.n}`)].join(', ');
                    const call = isStatic ? `${Q}::${name}(${args.map(a => a.n).join(', ')})`
                        : `self.${name}(${args.map(a => a.n).join(', ')})`;
                    return `[](${decl}){ return ${call}; }`;
                });
                s += `        t["${name}"] = sol::overload(${lams.join(', ')});\n`;
            }
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
const EXCLUDE = new Set(['Json', 'Xml', 'Shader', 'Thread', 'SoundSource', 'FileWriter', 'FileReader']);

let body = '', count = 0;
for (const id in data) {
    const e = data[id];
    if (e.kind !== 'type' || e.owner || e.ns) continue;
    if (EXCLUDE.has(e.name)) { report.push(`${e.name}: excluded (custom Lua glue, hand-written)`); continue; }
    if (e.tparams && e.tparams.length) {
        // templated type: instantiate per lua_bind entry, else report
        if (!e.lua_bind || !e.lua_bind.length) { report.push(`${e.name}: templated, no lua_bind (skipped)`); continue; }
        for (const T of e.lua_bind) {
            const base = e.name.replace(/<.*>/, '');
            try { body += emitType(e, `${base}<${T}>`, `${base}_${T.replace(/[^A-Za-z0-9]/g, '')}`, T); count++; }
            catch (err) { report.push(`${e.name}<${T}>: ${err.message}`); }
        }
        continue;
    }
    try { body += emitType(e, e.name, e.name, null); count++; }
    catch (err) { report.push(`${e.name}: ${err.message}`); }
}

process.stdout.write(`// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLua::setGeneratedTypeBindings(const std::shared_ptr<sol::state>& lua) {
${body}}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
`);
console.error(`[luagen-types] usertypes: ${count} | skipped members: template ${skip.template}, unbindable ${skip.unbindable}, unsupported-op ${skip.op}`);
for (const r of report) console.error('  ' + r);
