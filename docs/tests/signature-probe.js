#!/usr/bin/env node
// =============================================================================
// signature-probe.js — api-definition.yaml  ->  C++ signature check
// =============================================================================
//
// The authoritative "does every documented symbol exist with the documented
// signature?" check — the yaml->C++ direction (coverage-audit.js is the other
// way). For every documented function / method / operator it emits a line that
// static_casts the symbol's address to its documented signature:
//
//     static_cast<Ret (Owner::*)(P) const>(&Owner::name)
//
// so the COMPILER checks exact params + return + const-ness — no fragile string
// comparison. A `-fsyntax-only` compile is enough for these type checks; the
// existing example builds already cover linkage of out-of-line definitions.
// Each failed cast is mapped back to the yaml entry that produced it, and the
// compiler's "static_cast from 'REAL'" diagnostic gives the ground-truth
// signature to correct the yaml to.
//
//   node signature-probe.js              # human-readable mismatch report
//   node signature-probe.js --json       # machine-readable mismatch list
//   node signature-probe.js --strict     # exit non-zero if any mismatch (CI gate)
//   node signature-probe.js --keep       # keep the generated probe.cpp + map
//
// Report-only by default (like coverage-audit.js): the residual mixes genuine
// yaml signature lies with probe-generator limits (templates, protected hooks)
// that want a hidden/skip flag. Use --strict only once the yaml is probe-clean.
//
// On non-macOS hosts pass your own compile flags via TC_PROBE_CXXFLAGS
// (space-separated). Override the compiler with $CXX (default `c++`).

'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const yaml = require('js-yaml');

const ROOT = path.join(__dirname, '../../..');          // .../TrussC (outer)
const INCLUDE = path.join(ROOT, 'trussc/core/include');
const API_YAML = path.join(__dirname, '../api-definition.yaml');

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const JSON_OUT = has('--json'), STRICT = has('--strict'), KEEP = has('--keep');

const doc = yaml.load(fs.readFileSync(API_YAML, 'utf8'));

// ---------------------------------------------------------------------------
// 1. signature string ("float x, float y = 0") -> bare type list ("float, float")
// ---------------------------------------------------------------------------
function splitTopLevel(s) {
    const o = []; let d = 0, c = '';
    for (const ch of s) {
        if ('<(['.includes(ch)) d++; else if ('>)]'.includes(ch)) d--;
        if (ch === ',' && d === 0) { o.push(c); c = ''; } else c += ch;
    }
    if (c.trim()) o.push(c);
    return o;
}
function paramType(p) {
    let t = p.replace(/=.*$/, '').replace(/@/g, '').trim();   // strip default arg + AngelScript handle
    if (!t || t === 'void') return t === 'void' ? '' : null;
    // drop the trailing identifier (param name) if present
    const m = t.match(/^(.*[\w>\*&\s])\b([A-Za-z_]\w*)\s*$/);
    if (m) { const h = m[1].trim(); if (h && /[\w>\*&]/.test(h) && !/^(const|unsigned|signed|long|short|struct|class|enum)$/.test(h)) t = h; }
    return t.trim();
}
function typeList(sig) {
    if (!sig || !sig.trim()) return '';
    const parts = splitTopLevel(sig).map(paramType);
    if (parts.some((x) => x === null)) return null;
    return parts.filter((x) => x !== '').join(', ');
}
const stripHandle = (s) => (s || '').replace(/@/g, '').trim();   // AngelScript handle cruft
function cleanRet(s) {
    let t = stripHandle(s), prev;
    do { prev = t; t = t.replace(/^(static|constexpr|inline|virtual|friend|explicit)\s+/, '').trim(); } while (t !== prev);
    return t;
}

// templates / things we can't pointer-cast yet -> skip (handled later via test:/sample_types)
const TPL_OWNERS = new Set(['Tween', 'Event', 'EventListener', 'ThreadChannel']);
function isTemplated(owner, ret, params) {
    if (TPL_OWNERS.has(owner)) return true;
    const blob = (ret || '') + '|' + (params || '');
    return /<\s*T\s*>|<size_t|template|\.\.\./.test(blob);
}

// ---------------------------------------------------------------------------
// 2. emit the probe .cpp + a line -> yaml-entry map
// ---------------------------------------------------------------------------
const lines = ['#include <TrussC.h>', 'using namespace std;', 'using namespace tc;', ''];
const map = {};   // emitted line number (1-based) -> meta
const stat = { free: 0, method: 0, static: 0, typeMethod: 0, op: 0, freeOp: 0,
               skipAddon: 0, skipTpl: 0, skipNoRet: 0, skipParse: 0 };

function emit(meta, code) {
    const ln = lines.length + 1;            // 1-based line number of the code line
    lines.push(`void p${ln}(){ ${code} } //@ ${meta.tag}`);
    map[ln] = meta;
}

function emitCallable(entry, owner, isStatic, isConst, symbolName, sec) {
    const ret = cleanRet(entry.return);
    if (!ret) { stat.skipNoRet++; return; }
    if (entry.addon) { stat.skipAddon++; return; }
    if (entry.template) { stat.skipTpl++; return; }
    const sigs = (entry.signatures && entry.signatures.length) ? entry.signatures : [{ params: '' }];
    sigs.forEach((s, si) => {
        if (isTemplated(owner, ret, s.params)) { stat.skipTpl++; return; }
        const tl = typeList(s.params || '');
        if (tl === null) { stat.skipParse++; return; }
        const meta = { sec, owner: owner || null, name: entry.name, sigIdx: si, ret,
                       params: s.params || '', isStatic: !!isStatic, isConst: !!isConst, symbolName };
        let code;
        if (!owner) { code = `auto _p=static_cast<${ret}(*)(${tl})>(&${symbolName});(void)_p;`; stat.free++; }
        else if (isStatic) { code = `auto _p=static_cast<${ret}(*)(${tl})>(&${owner}::${symbolName});(void)_p;`; stat.static++; }
        else { const cst = isConst ? ' const' : ''; code = `auto _p=static_cast<${ret}(${owner}::*)(${tl})${cst}>(&${owner}::${symbolName});(void)_p;`; meta.member = true; if (sec === 'cat') stat.method++; else stat.typeMethod++; }
        meta.tag = `${sec}|${owner || '-'}|${entry.name}|${si}` + (meta.member ? '|m' : isStatic ? '|s' : '|f');
        emit(meta, code);
    });
}

// --- operators -------------------------------------------------------------
// const-ness is inferred from the symbol: compound-assign (+= -= *= /= ...) and
// [] mutate (non-const); value-returning binary/unary/comparison ops are const.
const opIsConst = (sym) => !(/=$/.test(sym) && !/^(==|!=|<=|>=)$/.test(sym)) && sym !== '[]';
const cppOp = (sym) => 'operator' + sym;            // "+" -> "operator+", "[]" -> "operator[]"

function emitMemberOp(op, owner, idx) {
    const ret = cleanRet(op.result);
    if (!ret) { stat.skipNoRet++; return; }
    if (isTemplated(owner, ret, op.rhs)) { stat.skipTpl++; return; }
    const tl = op.unary ? '' : (op.rhs || '').trim();   // rhs is already a bare type
    const cst = opIsConst(op.symbol) ? ' const' : '';
    const meta = { sec: 'op', owner, name: cppOp(op.symbol), sigIdx: idx, ret, params: tl, isConst: opIsConst(op.symbol), member: true };
    meta.tag = `op|${owner}|${op.symbol}|${idx}|m`;
    emit(meta, `auto _p=static_cast<${ret}(${owner}::*)(${tl})${cst}>(&${owner}::${cppOp(op.symbol)});(void)_p;`);
    stat.op++;
}
function emitFreeOp(op, ownerTag, idx) {
    const ret = cleanRet(op.result);
    if (!ret) { stat.skipNoRet++; return; }
    const tl = [op.lhs, op.rhs].map((x) => (x || '').trim()).filter(Boolean).join(', ');
    if (isTemplated(ownerTag, ret, tl)) { stat.skipTpl++; return; }
    const meta = { sec: 'op', owner: ownerTag || null, name: cppOp(op.symbol), sigIdx: idx, ret, params: tl, free: true };
    meta.tag = `op|${ownerTag || '-'}|${op.symbol}|${idx}|fo`;
    emit(meta, `auto _p=static_cast<${ret}(*)(${tl})>(&${cppOp(op.symbol)});(void)_p;`);
    stat.freeOp++;
}
// member ops live under `operators` (no lhs); free ops under `free_operators`, OR
// under `operators` carrying an `lhs` (the enum convention — enums have no members).
function emitOps(entry, owner) {
    (entry.operators || []).forEach((op, i) => op.lhs ? emitFreeOp(op, owner, i) : emitMemberOp(op, owner, i));
    (entry.free_operators || []).forEach((op, i) => emitFreeOp(op, owner, i));
}

const OWNER_ALIAS = { BaseApp: 'App' };   // hooks documented on BaseApp resolve to App
for (const c of (doc.categories || [])) {
    for (const f of (c.functions || [])) {
        const owner = f.self ? (OWNER_ALIAS[f.self] || f.self) : null;
        emitCallable(f, owner, !!f.static, !!f.const, f.cppName || f.name, 'cat');
    }
}
for (const t of (doc.types || [])) {
    for (const m of (t.methods || [])) emitCallable(m, t.name, !!m.static, !!m.const, m.cppName || m.name, 'type');
    emitOps(t, t.name);
}
for (const e of (doc.enums || [])) emitOps(e, e.name);

const probeCpp = lines.join('\n') + '\n';

// ---------------------------------------------------------------------------
// 3. compile -fsyntax-only and map each error back to its yaml entry
// ---------------------------------------------------------------------------
function defaultFlags() {
    return ['-DSOKOL_METAL', '-DTRUSSC_BUILD_DATE="probe"',
            '-I', INCLUDE, '-I', path.join(INCLUDE, 'sokol'),
            '-std=gnu++20', '-arch', 'arm64', '-mmacosx-version-min=14.0'];
}
const cxx = process.env.CXX || 'c++';
const extra = process.env.TC_PROBE_CXXFLAGS ? process.env.TC_PROBE_CXXFLAGS.split(/\s+/).filter(Boolean) : defaultFlags();
const tmpDir = KEEP ? __dirname : os.tmpdir();
const cppPath = path.join(tmpDir, 'tc_signature_probe.cpp');
fs.writeFileSync(cppPath, probeCpp);
if (KEEP) fs.writeFileSync(path.join(__dirname, 'tc_signature_probe.map.json'), JSON.stringify(map));

process.stderr.write(`signature-probe: compiling ${Object.keys(map).length} probes (${cxx} -fsyntax-only)…\n`);
const res = spawnSync(cxx, [...extra, '-fsyntax-only', '-ferror-limit=0', cppPath], { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });
const stderr = res.stderr || '';
if (!KEEP) try { fs.unlinkSync(cppPath); } catch {}

// Collect errors per probe line. The compiler's "static_cast from 'REAL'" (or
// "address of overloaded function ... cannot be static_cast to 'WANT'") gives
// the ground truth. We keep the first diagnostic line per probe line.
const base = path.basename(cppPath);
// clang echoes the path exactly as passed on the command line (absolute here),
// so match the basename preceded by a path separator or line start.
const errRe = new RegExp(`(?:^|[/\\\\])${base.replace(/[.]/g, '\\.')}:(\\d+):\\d+: error: (.+)$`);
const firstErrForLine = {};
for (const line of stderr.split('\n')) {
    const m = line.match(errRe);
    if (!m) continue;
    const ln = +m[1];
    if (map[ln] && firstErrForLine[ln] === undefined) firstErrForLine[ln] = m[2];
}

const mismatches = [];
for (const [ln, msg] of Object.entries(firstErrForLine)) {
    const meta = map[ln];
    const realM = msg.match(/static_cast from '([^']+)'/);
    mismatches.push({
        kind: meta.sec, owner: meta.owner, name: meta.name, sigIdx: meta.sigIdx,
        documented: signatureString(meta),
        real: realM ? realM[1] : null,
        diagnostic: msg,
    });
}
function signatureString(m) {
    const p = `(${typeList(m.params) ?? m.params})`;
    if (m.free || !m.owner) return `${m.ret} ${m.name}${p}`;
    if (m.isStatic) return `${m.ret} ${m.owner}::${m.name}${p} [static]`;
    return `${m.ret} ${m.owner}::${m.name}${p}${m.isConst ? ' const' : ''}`;
}

// ---------------------------------------------------------------------------
// 4. report
// ---------------------------------------------------------------------------
const total = Object.keys(map).length;
if (JSON_OUT) {
    process.stdout.write(JSON.stringify({ total, probed: stat, mismatchCount: mismatches.length, mismatches }, null, 2) + '\n');
} else {
    const byOwner = {};
    for (const mm of mismatches) (byOwner[mm.owner || '(free)'] ??= []).push(mm);
    console.log(`\nsignature-probe: ${total} probes, ${mismatches.length} mismatch(es)\n`);
    for (const owner of Object.keys(byOwner).sort()) {
        console.log(`  ${owner}`);
        for (const mm of byOwner[owner]) {
            console.log(`    ✗ ${mm.documented}`);
            if (mm.real) console.log(`      real: ${mm.real}`);
            else console.log(`      ${mm.diagnostic}`);
        }
    }
    const skipped = stat.skipTpl + stat.skipAddon + stat.skipNoRet + stat.skipParse;
    console.log(`\n  probed: ${stat.free} free, ${stat.method + stat.typeMethod} methods, ${stat.static} static, ${stat.op} ops, ${stat.freeOp} free-ops`);
    console.log(`  skipped: ${stat.skipTpl} templated, ${stat.skipAddon} addon, ${stat.skipNoRet} no-return, ${stat.skipParse} unparseable (${skipped} total)`);
    if (mismatches.length === 0) console.log('\n  ✓ every documented signature matches the real C++ API\n');
    else console.log('');
}

if (STRICT && mismatches.length > 0) process.exit(1);
