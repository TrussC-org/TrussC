#!/usr/bin/env node
// ============================================================================
// check.js — verification gate for the reference redesign (replaces the
// signature-probe + coverage-audit). Four checks, all simple set/load ops:
//
//   dup          a symbol-id (or sub-field) defined twice in the sidecar
//                -> HARD error (TOML spec; smol-toml throws on parse)
//   orphan       a sidecar key with no matching real symbol (renamed/removed)
//                -> HARD error (a doc that lies)
//   undocumented a public symbol with no prose
//                -> soft (report) until prose coverage is complete, then --strict
//   details      over-budget details in any language -> hard error with --strict
//
//   node check.js [--ast <cached>] [--structure <json>] [--toml <file>] [--strict] [--list]
//
// --strict makes `undocumented` and over-budget details hard errors too.
// --toml selects a sidecar (defaults to api-reference.toml). --structure reuses a
// precomputed structure.json (else it runs structure.js, which runs clang).
// ============================================================================
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const TOML = require(path.join(__dirname, '../node_modules/smol-toml'));

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const argVal = (f) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : null; };
const TOML_PATH = argVal('--toml') || path.join(__dirname, 'api-reference.toml');
// Characters mean UTF-16 code units (String.length); keep equal to
// API_NOTES_BUDGET_CHARACTERS in trussc-docs-ai/build-chunks.mjs. Check details
// per language here; the builder caps the whole formatted notes block, including
// related/examples, all details locales, keywords, snippets and platform notes.
// Bound (platform): a details text must fit one retrieval card's notes budget. Overflow fails the strict check with the entry and language named.
const API_NOTES_BUDGET_CHARACTERS = 10000;

// --- 1. dup: parse the sidecar (smol-toml rejects duplicate keys/sub-tables) -
let prose;
try { prose = TOML.parse(fs.readFileSync(TOML_PATH, 'utf8')); }
catch (e) { console.error(`✗ dup/parse: ${TOML_PATH} is invalid TOML\n  ${e.message.split('\n')[0]}`); process.exit(1); }
const keys = Object.keys(prose);

// --- 2. obtain canonical symbol-ids from the structure -----------------------
let ids;
const structFile = argVal('--structure');
if (structFile) {
    const structure = JSON.parse(fs.readFileSync(structFile, 'utf8'));
    ids = new Set(Object.keys(structure).filter(id => !structure[id].declaredIn || structure[id].declaredIn === structure[id].owner));
} else {
    const sArgs = ['--max-old-space-size=8192', path.join(__dirname, 'structure.js')];
    if (argVal('--ast')) sArgs.push('--ast', argVal('--ast'));
    sArgs.push('--ids');
    process.stderr.write('check: deriving structure (structure.js)…\n');
    ids = new Set(execFileSync('node', sArgs, { maxBuffer: 256 * 1024 * 1024 }).toString().trim().split('\n').filter(Boolean));
}

// --- 3. orphan + undocumented + details --------------------------------------
const orphans = keys.filter(k => !ids.has(k));
// undocumented = a symbol that belongs on the doc surface but has no description.
// Exempt: operators (rendered in their own section) and `hide = true` symbols
// (public C++ but not API). A bare keyword-only entry still counts as undocumented.
const undocumented = [...ids].filter(id => {
    if (/operator/.test(id)) return false;
    const p = prose[id];
    if (p && p.hide) return false;
    return !(p && p.description && p.description.en);
});
const longDetails = [];
for (const id of keys) {
    const details = prose[id].details;
    const localized = typeof details === 'string' ? { en: details } : details;
    for (const lang of ['en', 'ja', 'ko']) {
        const text = localized?.[lang];
        if (typeof text === 'string' && text.length > API_NOTES_BUDGET_CHARACTERS) {
            longDetails.push({ id, lang, length: text.length });
        }
    }
}

console.log('=== reference check ===');
console.log(`sidecar entries : ${keys.length}`);
console.log(`AST symbols     : ${ids.size}`);
console.log(`dup             : OK (TOML parsed; smol-toml enforces uniqueness)`);
console.log(`orphan          : ${orphans.length}${orphans.length ? '  ✗' : '  ✓'}`);
console.log(`undocumented    : ${undocumented.length} / ${ids.size}${has('--strict') ? (undocumented.length ? '  ✗ (strict)' : '  ✓') : '  (soft)'}`);
console.log(`details over cap: ${longDetails.length}${has('--strict') ? (longDetails.length ? '  ✗ (strict)' : '  ✓') : '  (soft)'}`);

if (orphans.length) { console.log('\n--- ORPHAN keys (no such symbol — rename/remove the entry) ---'); orphans.slice(0, has('--list') ? 1e9 : 40).forEach(k => console.log('  ' + k)); }
if (has('--list') && undocumented.length) { console.log('\n--- undocumented symbols (need prose) ---'); undocumented.forEach(id => console.log('  ' + id)); }
for (const { id, lang, length } of longDetails) {
    console.log(`  ${has('--strict') ? '✗ error' : 'warning'}: ${id} details.${lang}: ${length} characters (UTF-16 code units) exceeds ${API_NOTES_BUDGET_CHARACTERS}; split or shorten it.`);
}

const fail = orphans.length > 0 || (has('--strict') && (undocumented.length > 0 || longDetails.length > 0));
process.exit(fail ? 1 : 0);
