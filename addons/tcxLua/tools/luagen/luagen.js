#!/usr/bin/env node
// luagen.js — generate tcxLua (Sol2) Lua bindings from reference-data.json.
//
// reference-data.json (docs/reference/) is the canonical artifact: structure from
// the real C++ AST + prose, keyed by symbol-id. Each signature carries a structured
// `args[]` ({type, name, hasDefault, isRef?, isConst?, isPointer?, isArray?}), so
// binding generation is fully DETERMINISTIC — no string parsing, no heuristics, no
// denylist. Correctness is verified by COMPILING this output (witness).
//
//   node luagen.js <reference-data.json> > trussc_generated.cpp   (report -> stderr)
//
// Phase 1 = top-level free functions (kind:func, no owner, no ns). Type members
// (owner) and sub-namespaced funcs (ns -> Lua tables) are Phase 2.

const fs = require('fs');
const path = process.argv[2];
if (!path) { console.error('usage: node luagen.js <reference-data.json>'); process.exit(1); }
const data = JSON.parse(fs.readFileSync(path, 'utf8'));

const skip = { member: 0, ns: 0, template: 0, unbindable: 0, noargs: 0 };
const noargsFns = [];   // signatures missing structured args[] (structure.js gap)

// Primitive/scalar type words — a non-const ref to one of these is a true
// out-param Sol2 can't bind (can't bind a temporary to `float&`). A non-const
// ref to a USERTYPE (Light&, Pixels&) IS bindable (Lua object passed by ref).
const PRIM = new Set([
    'void', 'bool', 'char', 'short', 'int', 'long', 'float', 'double',
    'unsigned', 'signed', 'size_t', 'wchar_t', 'char16_t', 'char32_t',
    'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
]);

// An arg is bindable unless it's a raw pointer, a C array, or a non-const
// reference to a primitive (a real out-param). (shared_ptr<T> isn't isPointer.)
function argBindable(a) {
    if (a.isArray) return false;
    if (a.isPointer) return false;
    if (a.isRef && !a.isConst && !/&&/.test(a.type)) {
        // A non-const ref is a bindable in/out only for a USERTYPE (TrussC class,
        // passed by ref). For a value Sol2 converts (primitive or std type) it's a
        // true out-param it can't bind. Keep only non-primitive, non-std bases.
        const base = a.type.replace(/[&*]/g, '').replace(/\bconst\b/g, '').trim();
        const allPrim = base.split(/\s+/).every(w => PRIM.has(w));
        const stdValue = base.includes('std::') || /\b(string|basic_string|vector|map|set|pair|tuple|function)\b/.test(base);
        if (allPrim || stdValue) return false;   // out-param Sol2 can't bind
    }
    return true;
}

// One {decl, call} per callable arity (trailing defaults expand into overloads),
// or null if any arg is unbindable.
function variantsOf(args) {
    for (const a of args) if (!argBindable(a)) return null;
    const decls = [], calls = [];
    let firstDefault = args.length;
    args.forEach((a, i) => {
        if (a.hasDefault && firstDefault === args.length) firstDefault = i;
        const nm = a.name || `a${i}`;
        decls.push(`${a.type} ${nm}`.replace(/\s+/g, ' ').trim());
        calls.push(nm);
    });
    const out = [];
    for (let k = firstDefault; k <= args.length; k++) {
        out.push({ decl: decls.slice(0, k).join(', '), call: calls.slice(0, k).join(', ') });
    }
    return out;
}

function emit(e) {
    if (e.tparams && e.tparams.length) { skip.template++; return ''; }
    // `provider:"std"` symbols (sin/cos/min…) are std's, not trussc:: members —
    // call them qualified as std::. Everything else is a trussc:: free function.
    const callName = e.provider === 'std' ? `std::${e.name}` : `trussc::${e.name}`;
    const lambdas = [];
    for (const sig of (e.signatures || [])) {
        if (sig.tmpl) { skip.template++; continue; }   // template-derived phantom sig (e.g. typeName<T>())
        // No structured args[] — can't bind deterministically; skip + report.
        if (!sig.args) { skip.noargs++; noargsFns.push(e.name); continue; }
        const vs = variantsOf(sig.args);
        if (!vs) continue;                        // unbindable arg in this overload
        const refRet = /&\s*$/.test(sig.ret || '');   // preserve reference (non-copyable) returns
        for (const v of vs) {
            const body = `return ${callName}(${v.call});`;
            lambdas.push(refRet
                ? `[](${v.decl}) -> decltype(auto) { ${body} }`
                : `[](${v.decl}) { ${body} }`);
        }
    }
    const uniq = [...new Set(lambdas)];
    if (uniq.length === 0) { skip.unbindable++; return ''; }
    if (uniq.length === 1) return `    lua->set_function("${e.name}", ${uniq[0]});\n`;
    return `    lua->set_function("${e.name}", sol::overload(\n        ${uniq.join(',\n        ')}\n    ));\n`;
}

let body = '', count = 0;
for (const id of Object.keys(data)) {
    const e = data[id];
    if (e.kind !== 'func') continue;
    if (e.owner) { skip.member++; continue; }    // type member -> Phase 2
    if (e.ns) { skip.ns++; continue; }           // sub-namespaced -> Phase 2 (Lua tables)
    const s = emit(e);
    if (s) { body += s; count++; }
}

process.stdout.write(`// AUTO-GENERATED from reference-data.json by luagen.js — DO NOT EDIT.
#include "tcxLua.h"
#include "TrussC.h"

using namespace trussc;
using namespace std;

#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif

void tcxLua::setTrussCGeneratedBindings(const std::shared_ptr<sol::state>& lua) {
${body}}

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
`);

console.error(`[luagen] bound free functions: ${count} | skipped: members ${skip.member}, ns ${skip.ns}, templates ${skip.template}, unbindable ${skip.unbindable}, no-args(structure.js gap) ${skip.noargs}`);
if (noargsFns.length) console.error(`  missing args[] (report to structure.js): ${[...new Set(noargsFns)].join(', ')}`);
