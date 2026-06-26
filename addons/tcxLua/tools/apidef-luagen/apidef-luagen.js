#!/usr/bin/env node
// apidef-luagen.js — generate tcxLua (Sol2) Lua bindings from api-definition.yaml.
//
// PoC. Phase 1 = FREE FUNCTIONS only (the slice the libclang bindgen covers today).
// Reads the canonical YAML and emits a drop-in trussc_generated.cpp that defines
// tcxLua::setTrussCGeneratedBindings(). Type members (usertypes/operators) = Phase 2.
//
//   node apidef-luagen.js <api-definition.yaml> > trussc_generated.cpp
//   (coverage + skip report go to stderr)
//
// Design notes:
//  - Free function = a `categories[].functions[]` entry WITHOUT a `self:` field
//    (entries with `self:` are type members -> Phase 2).
//  - Lambda params are built by pairing the typed list (`params`) with the names
//    (`params_simple`); if a typed token lacks the name, the name is appended.
//    This needs the two to agree in arity — mismatches are reported & skipped
//    (they'd be a YAML data bug; the generated .cpp also wouldn't compile).
//  - Correctness is ultimately guaranteed by COMPILING this output against the
//    real headers (witness principle): a wrong name/type fails the build.

const fs = require('fs');
const yaml = require('js-yaml');

const yamlPath = process.argv[2];
if (!yamlPath) { console.error('usage: node apidef-luagen.js <api-definition.yaml>'); process.exit(1); }
const api = yaml.load(fs.readFileSync(yamlPath, 'utf8'));

const NS = 'trussc';
const warnings = [];

// Functions that are documented but NOT Lua-bindable as a plain lambda:
//  - template entry points (runApp<App>), etc.
// (Long-term this should be a `bindable: false` / lua-exclude flag in the YAML.)
const DENYLIST = new Set([
    'runApp', 'runHeadlessApp',            // template entry points
    // Sound is being restructured into types (ChipSoundNote props / AudioEngine
    // events) upstream; these stale free-function entries move to Phase 2 then.
    'audioOut', 'wave', 'hz', 'duration', 'volume', 'attack', 'decay', 'sustain', 'release', 'adsr',
]);

// Names that exist in BOTH trussc:: and std:: — `using`-directives make them
// ambiguous, so force the trussc:: qualifier. (Long-term: a namespace hint in YAML.)
const FORCE_NS = new Set(['random']);

// Split a C++ parameter list on top-level commas (ignore commas inside <> or ()).
function splitParams(s) {
    if (!s || !s.trim()) return [];
    const out = []; let depth = 0, cur = '';
    for (const ch of s) {
        if (ch === '<' || ch === '(') depth++;
        else if (ch === '>' || ch === ')') depth--;
        if (ch === ',' && depth === 0) { out.push(cur.trim()); cur = ''; }
        else cur += ch;
    }
    if (cur.trim()) out.push(cur.trim());
    return out;
}

// C++ words that are part of a TYPE, so a param ending in one has no arg name.
const TYPE_WORDS = new Set([
    'void', 'bool', 'char', 'short', 'int', 'long', 'float', 'double',
    'unsigned', 'signed', 'size_t', 'wchar_t', 'char16_t', 'char32_t', 'auto',
    'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
]);

// Split one (default-stripped) param into {type, name|null}. The arg name, if any,
// is the trailing identifier (unless it's a bare type word like "unsigned int").
function splitTypeName(token) {
    const t = token.trim();
    const m = t.match(/^(.*\S)\s+([A-Za-z_]\w*)$/);
    if (m && !TYPE_WORDS.has(m[2])) return { type: m[1].trim(), name: m[2] };
    return { type: t, name: null };
}

// Build lambda {decl, call} variants from the typed param list ALONE (params_simple
// is no longer needed): use the embedded arg name where present, else synthesize
// a0/a1/... Trailing default args expand into multiple arities.
function buildLambdaVariants(paramsTyped) {
    if (/\bT\b/.test(paramsTyped)) return { error: `template signature (bare T): "${paramsTyped}"` };
    const tokens = splitParams(paramsTyped);
    if (tokens.length === 0) return { variants: [{ decl: '', call: '' }] };

    const decls = [], callNames = [];
    let firstDefault = tokens.length;
    for (let i = 0; i < tokens.length; i++) {
        let tok = tokens[i];
        if (tok.includes('=')) {                    // strip default value
            if (firstDefault === tokens.length) firstDefault = i;
            tok = tok.slice(0, tok.indexOf('=')).trim();
        }
        const { type, name } = splitTypeName(tok);
        const argName = name || `a${i}`;            // synthesize when type-only
        callNames.push(argName);
        decls.push(name ? tok : `${type} ${argName}`);
    }
    // One variant per arity from firstDefault..all (optional args become overloads).
    const variants = [];
    for (let k = firstDefault; k <= tokens.length; k++) {
        variants.push({ decl: decls.slice(0, k).join(', '), call: callNames.slice(0, k).join(', ') });
    }
    return { variants };
}

function emitFn(name, fn) {
    // Call unqualified (resolves via `using namespace trussc; using namespace std;`),
    // except names that clash between the two namespaces (FORCE_NS -> trussc::).
    const callName = FORCE_NS.has(name) ? `${NS}::${name}` : name;
    // Preserve the return only for reference-returning funcs (e.g. CoreEvents&,
    // Logger& — non-copyable). Value returns must NOT use decltype(auto), or
    // std-style funcs that return a ref to an arg (min/max) would dangle.
    const refReturn = (fn.return || '').includes('&');
    const lambdas = [];
    const seen = new Set();
    for (const sig of (fn.signatures || [])) {
        const r = buildLambdaVariants(sig.params || '');
        if (r.error) { warnings.push(`skip ${name}(${sig.params}): ${r.error}`); continue; }
        for (const v of r.variants) {
            const lam = refReturn
                ? `[](${v.decl}) -> decltype(auto) { return ${callName}(${v.call}); }`
                : `[](${v.decl}) { return ${callName}(${v.call}); }`;
            if (seen.has(lam)) continue;            // dedup identical arities across sigs
            seen.add(lam);
            lambdas.push(lam);
        }
    }
    if (lambdas.length === 0) { warnings.push(`skip ${name}: no usable signatures`); return ''; }
    if (lambdas.length === 1) return `    lua->set_function("${name}", ${lambdas[0]});\n`;
    return `    lua->set_function("${name}", sol::overload(\n        ${lambdas.join(',\n        ')}\n    ));\n`;
}

// Collect free functions (no `self`), dedup by name.
const byName = new Map();
let typeMembers = 0;
for (const cat of (api.categories || [])) {
    for (const fn of (cat.functions || [])) {
        if (fn.self) { typeMembers++; continue; }   // -> Phase 2
        if (!fn.signatures) continue;
        // `lua:` marks bindability; ABSENT means true (most symbols). Only an
        // explicit `lua: false` excludes. (Replaces the structural denylist below
        // once the YAML carries lua: false on non-bindable symbols.)
        if (fn.lua === false) { warnings.push(`exclude ${fn.name}: lua: false`); continue; }
        if (DENYLIST.has(fn.name)) { warnings.push(`exclude ${fn.name}: denylist (non-bindable)`); continue; }
        if (fn.name.includes('::')) { warnings.push(`exclude ${fn.name}: namespaced (not a plain Lua global)`); continue; }
        if (!byName.has(fn.name)) byName.set(fn.name, fn);
        else warnings.push(`duplicate free-function name: ${fn.name} (keeping first)`);
    }
}

let body = '';
let emitted = 0;
for (const [name, fn] of byName) {
    const s = emitFn(name, fn);
    if (s) { body += s; emitted++; }
}

const out = `// AUTO-GENERATED from api-definition.yaml by apidef-luagen.js — DO NOT EDIT.
#include "tcxLua.h"
#include "TrussC.h"

using namespace trussc;
using namespace std;   // YAML param types use unqualified string/vector (canonical style)

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
`;
process.stdout.write(out);

console.error(`[apidef-luagen] free-function names: ${byName.size}, emitted: ${emitted}, type-members skipped (Phase 2): ${typeMembers}, warnings: ${warnings.length}`);
for (const w of warnings) console.error('  WARN ' + w);
