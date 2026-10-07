#!/usr/bin/env node
// emit-coverage.js — generate AllFeaturesExample's API coverage file from
// reference-data.json (a downstream consumer; see README.md).
//
//   node emit-coverage.js [--data <reference-data.json>] [--out <file.cpp>]
//
// Every documented public core function, method, field, constant and
// constructor is referenced once, with arguments of exactly the declared types,
// inside code that never runs (af::never). That makes every CI platform that
// builds AllFeaturesExample (desktop, Android, web and iOS on each PR and
// merge-queue run; desktop and web again in the daily run) compile each call against the headers AND link it against that platform's
// implementation — a missing per-platform implementation, or a TC_PLATFORMS
// list that promises a platform which doesn't have it, fails the build.
//
// Symbols restricted with TC_PLATFORMS("...") are wrapped in the matching
// #if (AF_* macros from coverage.h). Skipped: deprecated signatures (the repo
// builds with TC_DEPRECATED_ERRORS), templates (hand-covered in
// coverage_manual.cpp), non-public members, and the entries in SKIP below.
//
// Re-run after an API change, alongside emit-forai.js / emit-of.js.
// Deprecating, removing or renaming a documented API, or dropping a
// platform's implementation along with its TC_PLATFORMS entry, needs the
// regenerated coverage_generated.cpp in the SAME PR: the committed file still
// calls the old API, so CI fails (-Werror=deprecated-declarations via
// TC_DEPRECATED_ERRORS, or a compile / link error). Steps from a fresh
// checkout: build once (generates the shader headers generate.js needs), then
// `node generate.js`, then `node emit-coverage.js`.

'use strict';
const fs = require('fs');
const path = require('path');

const argv = process.argv.slice(2);
const argVal = (flag, dflt) => { const i = argv.indexOf(flag); return i >= 0 ? argv[i + 1] : dflt; };
const DATA = argVal('--data', path.join(__dirname, 'reference-data.json'));
const OUT = argVal('--out', path.join(__dirname, '../../examples/tests/AllFeaturesExample/src/coverage_generated.cpp'));

const data = JSON.parse(fs.readFileSync(DATA, 'utf8'));

// Symbols (ids) or owners ("Owner::*") that can't be referenced this way, with
// the reason. Keep this short: each entry is an API the canary does not check.
const SKIP = {
    'runApp': 'template entry point; the example itself is launched with it',
    'runHeadlessApp': 'template entry point',
    'SoundSource': 'abstract: no constructor to reference (its methods are covered)',
    'Thread': 'abstract: no constructor to reference (its methods are covered)',
    'VideoPlayerBase': 'abstract: no constructor to reference (its methods are covered)',
};
const skipReason = (id, owner) =>
    (Object.prototype.hasOwnProperty.call(SKIP, id) && SKIP[id]) ||
    (owner && Object.prototype.hasOwnProperty.call(SKIP, `${owner}::*`) && SKIP[`${owner}::*`]) || null;

const PLATFORM_MACRO = {
    macos: 'AF_MACOS', windows: 'AF_WINDOWS', linux: 'AF_LINUX',
    ios: 'AF_IOS', android: 'AF_ANDROID', web: 'AF_WEB',
};

function platformGuard(platforms) {
    if (!platforms || !platforms.length) return null;
    const parts = platforms.map(p => PLATFORM_MACRO[p]).filter(Boolean).map(m => `defined(${m})`);
    return parts.length ? parts.join(' || ') : '0';
}

// "const std::string &" -> af::val<const std::string>()
// "Vec3 &&"            -> std::move(af::val<Vec3>())
function argExpr(type) {
    let t = type.trim();
    let rvalue = false;
    if (t.endsWith('&&')) { rvalue = true; t = t.slice(0, -2).trim(); }
    else if (t.endsWith('&')) { t = t.slice(0, -1).trim(); }
    const e = `af::val<${t}>()`;
    return rvalue ? `std::move(${e})` : e;
}

function callArgs(sig) {
    return (sig.args || []).map(a => argExpr(a.type)).join(', ');
}

// provider 'std': std:: functions documented for users (sin, lerp, ...), not TrussC's.
const usable = (v) => v.documented !== false && !v.hidden && !v.deprecated && !v.provider &&
                      (!v.access || v.access === 'public');
const usableSig = (s) => !s.deprecated && !s.tmpl;
const qualify = (v) => (v.ns ? `${v.ns}::` : '') + v.name;

// Owners that are class templates (Tween<T>, Event<T>, ...) are hand-covered.
const templateOwners = new Set(
    Object.values(data).filter(v => v.kind === 'type' && v.tparams && v.tparams.length).map(v => v.name));

const skipped = [];
const free = {};     // category -> [lines]
const owners = {};   // owner -> [lines]

function push(map, key, guard, lines) {
    (map[key] = map[key] || []).push({ guard, lines });
}

for (const [id, v] of Object.entries(data)) {
    const reason = skipReason(id, v.owner);
    if (reason) { skipped.push(`${id}: ${reason}`); continue; }
    if (!usable(v)) continue;
    if (v.owner && (templateOwners.has(v.owner) || v.owner.includes('<'))) continue;
    const guard = platformGuard(v.platforms);

    if (v.kind === 'func') {
        const lines = v.signatures.filter(usableSig).map(s => `(void)${qualify(v)}(${callArgs(s)});`);
        if (lines.length) push(free, v.category || 'other', guard, lines);
    } else if (v.kind === 'var' && !v.owner) {
        push(free, v.category || 'other', guard, [`(void)${qualify(v)};`]);
    } else if (v.kind === 'method' && v.owner) {
        if (v.name === v.owner || v.name.startsWith('~')) continue;   // ctors via 'constructors', no dtors
        const lines = v.signatures.filter(usableSig).map(s => `(void)af::val<${v.owner}>().${v.name}(${callArgs(s)});`);
        if (lines.length) push(owners, v.owner, guard, lines);
    } else if (v.kind === 'field' && v.owner) {
        push(owners, v.owner, guard, [`(void)af::val<${v.owner}>().${v.name};`]);
    } else if (v.kind === 'type' && v.constructors && v.constructors.length && !v.owner &&
               !(v.tparams && v.tparams.length)) {
        const lines = v.constructors.filter(usableSig).map(s => `(void)${qualify(v)}(${callArgs(s)});`);
        if (lines.length) push(owners, v.name, guard, lines);
    }
}

function emitBlock(entries, indent) {
    let out = '';
    for (const { guard, lines } of entries) {
        if (guard) out += `#if ${guard}\n`;
        for (const l of lines) out += `${indent}${l}\n`;
        if (guard) out += `#endif\n`;
    }
    return out;
}

const sanitize = (s) => s.replace(/[^A-Za-z0-9_]/g, '_');
let cpp = `// GENERATED by docs/reference/emit-coverage.js from reference-data.json.
// Do not edit by hand: re-run the generator after an API change.
//
// References every documented public core API once, inside code that never
// runs (see coverage.h), so each platform that builds AllFeaturesExample
// compiles the call and links it against its own implementation.

#include "coverage.h"

namespace trussc {
namespace af_generated {

`;
const freeCats = Object.keys(free).sort();
for (const cat of freeCats) {
    cpp += `static void cover_${sanitize(cat)}() {\n${emitBlock(free[cat], '    ')}}\n\n`;
}
// Methods run inside a struct derived from the owner, so owner-relative type
// names in signatures (Node's Ptr, ...) resolve by ordinary member lookup.
// An owner that can't be derived from (a final class) would not compile here.
// None is documented today; if one appears, list both 'Foo' and 'Foo::*' in
// SKIP (with the reason) — that drops all of Foo's coverage.
const ownerNames = Object.keys(owners).sort();
for (const o of ownerNames) {
    cpp += `struct Cover_${sanitize(o)} : af::Scope<${o}> {\n    static void run() {\n${emitBlock(owners[o], '        ')}    }\n};\n\n`;
}
cpp += `} // namespace af_generated

void af::coverGenerated() {
`;
for (const cat of freeCats) cpp += `    af_generated::cover_${sanitize(cat)}();\n`;
for (const o of ownerNames) cpp += `    af_generated::Cover_${sanitize(o)}::run();\n`;
cpp += `}

} // namespace trussc
`;

fs.writeFileSync(OUT, cpp);
const nFree = freeCats.reduce((n, c) => n + free[c].reduce((m, e) => m + e.lines.length, 0), 0);
const nOwn = ownerNames.reduce((n, o) => n + owners[o].reduce((m, e) => m + e.lines.length, 0), 0);
console.log(`wrote ${path.relative(process.cwd(), OUT)}: ${nFree} free-function/constant references in ${freeCats.length} categories, ${nOwn} member references on ${ownerNames.length} types`);
if (skipped.length) console.log(`skipped by rule:\n  ${skipped.join('\n  ')}`);
