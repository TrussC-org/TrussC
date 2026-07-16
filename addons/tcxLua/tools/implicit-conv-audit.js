#!/usr/bin/env node
/**
 * implicit-conv-audit.js — enumerate every bound function/method argument that
 * relies on a C++ implicit conversion Lua cannot perform.
 *
 * A C++ call like drawLine(vec2a, vec2b) against drawLine(Vec3, Vec3) works
 * because Vec3 has a non-explicit Vec3(const Vec2&) constructor. sol2 knows
 * nothing about C++ converting constructors: with safeties off the wrong-type
 * argument is UB (garbage reinterpret for userdata, NULL deref for
 * non-userdata like numbers/strings). This script cross-references
 * reference-data.json (all bound signatures x all type constructors) to list
 * every such site, so adapters (see tcxLuaPathAdapter.h) can be added where
 * the conversion is worth supporting.
 *
 *   node implicit-conv-audit.js [path/to/reference-data.json]
 *
 * `explicit` is not captured in the AST extraction, so it is recovered by
 * scanning core/include headers for `explicit TypeName(...)` and matching the
 * first parameter's base type.
 */
const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const RDJ = process.argv[2] ||
    path.join(__dirname, '../../../docs/reference/reference-data.json');
const CORE_INCLUDE = path.join(__dirname, '../../../core/include');
const data = JSON.parse(fs.readFileSync(RDJ, 'utf8'));

// ---- helpers -----------------------------------------------------------------
const baseType = (t) => (t || '')
    .replace(/&&?/g, '').replace(/\bconst\b/g, '').replace(/\btrussc::/g, '')
    .replace(/\bstd::filesystem::/g, 'fs::').trim();
const NUMBER = new Set(['float', 'double', 'int', 'unsigned', 'unsigned int',
    'long', 'size_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t',
    'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t', 'short', 'char']);
const STRING = new Set(['std::string', 'string', 'std::string_view',
    'string_view', 'const char *', 'char *']);
const luaKind = (bt) => NUMBER.has(bt) ? 'number'
    : STRING.has(bt) ? 'string'
    : bt === 'bool' ? 'bool'
    : bt === 'fs::path' || bt === 'path' ? 'path'
    : null; // null = not natively representable (class type / other)

// ---- collect bound top-level types + their converting constructors ------------
const types = {};          // name -> entry
for (const id in data) {
    const e = data[id];
    if (e.kind === 'type' && !e.owner && !e.ns && !e.hidden) types[e.name] = e;
}
const enums = new Set();
for (const id in data) {
    const e = data[id];
    if (e.kind === 'enum' && !e.owner && !e.ns) enums.add(e.name);
}

// explicit-ctor recovery: "explicit TypeName(" + first param base type
const explicitSet = new Set(); // "Type<-FirstParamBase"
try {
    const out = execSync(
        `grep -rhoE "explicit +[A-Za-z_][A-Za-z0-9_]* *\\([^,)]*" ${JSON.stringify(CORE_INCLUDE)}`,
        { encoding: 'utf8', maxBuffer: 1 << 24 });
    for (const line of out.split('\n')) {
        const m = line.match(/explicit +([A-Za-z_][A-Za-z0-9_]*) *\(([^,)]*)/);
        if (!m) continue;
        const first = baseType(m[2].replace(/[A-Za-z_][A-Za-z0-9_]*$/, '').trim() || m[2]);
        explicitSet.add(`${m[1]}<-${first}`);
    }
} catch (e) { /* grep found nothing */ }

// converting ctors: exactly 1 required arg, source != self
const convSources = {}; // TypeName -> [{from, fromKind, params, explicit}]
for (const name in types) {
    for (const c of (types[name].constructors || [])) {
        const args = c.args || [];
        const required = args.filter(a => !a.hasDefault);
        if (required.length !== 1) continue;
        const from = baseType(required[0].type);
        if (from === name) continue; // copy/move
        const kind = luaKind(from) || (types[from] ? 'usertype' : (enums.has(from) ? 'enum' : 'other'));
        const explicit = explicitSet.has(`${name}<-${from}`);
        (convSources[name] ||= []).push({ from, fromKind: kind, params: c.params, explicit });
    }
}

// ---- walk every bound callable's args -----------------------------------------
// Mirrors luagen bindability: skip template sigs, pointer/array args.
const sites = {};   // "To<-From" -> {explicit, examples: [], count}
let usertypeArgSites = 0; // context stat for the SOL-safeties decision
function scanSignature(ownerLabel, fnName, sig) {
    if (sig.tmpl) return;
    for (const a of (sig.args || [])) {
        if (a.isPointer || a.isArray) return; // sig not bound at all
    }
    for (const a of (sig.args || [])) {
        const bt = baseType(a.type);
        if (!types[bt]) continue;             // not a class-type arg
        usertypeArgSites++;
        for (const cs of (convSources[bt] || [])) {
            if (cs.fromKind === 'other') continue;   // not reachable from Lua anyway
            const key = `${bt} <- ${cs.from}${cs.explicit ? '  [explicit in C++]' : ''}`;
            const s = (sites[key] ||= { count: 0, examples: [], explicit: cs.explicit });
            s.count++;
            if (s.examples.length < 8)
                s.examples.push(`${ownerLabel}${fnName}(${(sig.args || []).map(x => baseType(x.type)).join(', ')})`);
        }
    }
}
for (const id in data) {
    const e = data[id];
    if (e.hidden || e.deprecated) continue;
    if (e.kind === 'func' && !e.owner && !e.ns) {
        for (const sig of (e.signatures || [])) scanSignature('', e.name, sig);
    } else if (e.kind === 'method' && e.owner) {
        if (e.access && e.access !== 'public') continue;
        const owner = data[e.owner];
        if (!owner || owner.hidden) continue;
        for (const sig of (e.signatures || [])) scanSignature(owner.name + ':', e.name, sig);
    }
}

// ---- report --------------------------------------------------------------------
const entries = Object.entries(sites).sort((a, b) => b[1].count - a[1].count);
console.log('== Implicit-conversion reliance sites (bound args whose C++ type has a');
console.log('   converting ctor from a Lua-representable source) ==\n');
for (const [key, s] of entries) {
    console.log(`${key}   — ${s.count} arg site(s)`);
    for (const ex of s.examples) console.log(`    ${ex}`);
    if (s.count > s.examples.length) console.log(`    ... +${s.count - s.examples.length} more`);
    console.log('');
}
console.log(`(context: ${usertypeArgSites} bound arg sites take a usertype at all — every one`);
console.log(` of them is UB on a wrong-typed argument while sol safeties are off)`);
