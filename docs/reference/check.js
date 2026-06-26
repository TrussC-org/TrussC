#!/usr/bin/env node
// ============================================================================
// check.js — verification gate for the reference redesign (replaces the
// signature-probe + coverage-audit). Three checks, all simple set/load ops:
//
//   dup          a symbol-id (or sub-field) defined twice in the sidecar
//                -> HARD error (TOML spec; smol-toml throws on parse)
//   orphan       a sidecar key with no matching real symbol (renamed/removed)
//                -> HARD error (a doc that lies)
//   undocumented a public symbol with no prose
//                -> soft (report) until prose coverage is complete, then --strict
//
//   node check.js [--ast <cached>] [--structure <json>] [--strict] [--list]
//
// --strict makes `undocumented` a hard error too. --structure reuses a
// precomputed structure.json (else it runs structure.js, which runs clang).
// ============================================================================
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const TOML = require(path.join(__dirname, '../node_modules/smol-toml'));

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const argVal = (f) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : null; };
const TOML_PATH = path.join(__dirname, 'api-reference.toml');

// --- 1. dup: parse the sidecar (smol-toml rejects duplicate keys/sub-tables) -
let prose;
try { prose = TOML.parse(fs.readFileSync(TOML_PATH, 'utf8')); }
catch (e) { console.error(`✗ dup/parse: ${TOML_PATH} is invalid TOML\n  ${e.message.split('\n')[0]}`); process.exit(1); }
const keys = Object.keys(prose);

// --- 2. obtain canonical symbol-ids from the structure -----------------------
let ids;
const structFile = argVal('--structure');
if (structFile) {
    ids = new Set(Object.keys(JSON.parse(fs.readFileSync(structFile, 'utf8'))));
} else {
    const sArgs = ['--max-old-space-size=8192', path.join(__dirname, 'structure.js')];
    if (argVal('--ast')) sArgs.push('--ast', argVal('--ast'));
    sArgs.push('--ids');
    process.stderr.write('check: deriving structure (structure.js)…\n');
    ids = new Set(execFileSync('node', sArgs, { maxBuffer: 256 * 1024 * 1024 }).toString().trim().split('\n').filter(Boolean));
}

// --- 3. orphan + undocumented ------------------------------------------------
const orphans = keys.filter(k => !ids.has(k));
const undocumented = [...ids].filter(id => !keys.has?.(id) && !prose[id]);

console.log('=== reference check ===');
console.log(`sidecar entries : ${keys.length}`);
console.log(`AST symbols     : ${ids.size}`);
console.log(`dup             : OK (TOML parsed; smol-toml enforces uniqueness)`);
console.log(`orphan          : ${orphans.length}${orphans.length ? '  ✗' : '  ✓'}`);
console.log(`undocumented    : ${undocumented.length} / ${ids.size}${has('--strict') ? (undocumented.length ? '  ✗ (strict)' : '  ✓') : '  (soft)'}`);

if (orphans.length) { console.log('\n--- ORPHAN keys (no such symbol — rename/remove the entry) ---'); orphans.slice(0, has('--list') ? 1e9 : 40).forEach(k => console.log('  ' + k)); }
if (has('--list') && undocumented.length) { console.log('\n--- undocumented symbols (need prose) ---'); undocumented.forEach(id => console.log('  ' + id)); }

const fail = orphans.length > 0 || (has('--strict') && undocumented.length > 0);
process.exit(fail ? 1 : 0);
