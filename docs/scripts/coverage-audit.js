#!/usr/bin/env node
// =============================================================================
// coverage-audit.js — C++ public API  ->  api-definition.yaml coverage check
// =============================================================================
//
// The authoritative "is every public symbol documented?" check. It enumerates
// the REAL public core API from the clang AST (the compiler is the oracle, same
// philosophy as the signature probe), drops anything under an excluded namespace
// (declared in api-definition.yaml `coverage.excluded_namespaces`), then diffs
// what remains against the documented symbols in the yaml.
//
//   node coverage-audit.js                 # human-readable grouped report
//   node coverage-audit.js --json          # machine-readable residual list
//   node coverage-audit.js --ast <file>    # reuse a cached AST dump (skip clang)
//   node coverage-audit.js --save-ast <f>  # write the AST dump to <f> and exit
//
// Report-only: it never fails the build. The residual is a MIX of genuine doc
// gaps and intentional-but-public internals; bucketing it into document /
// hidden / ignore is a separate one-time triage (the closed-world pass). New
// public API added later with no yaml entry shows up here immediately.
//
// Cost: a full `#include <TrussC.h>` AST dump (~280 MB). The script re-execs
// itself with a larger V8 heap. On non-macOS hosts pass your own compile flags
// via TC_COVERAGE_CXXFLAGS (space-separated) — the public surface is mostly
// platform-independent, but the dump must compile.

'use strict';
const fs = require('fs');
const path = require('path');
const os = require('os');
const { execFileSync, spawnSync } = require('child_process');
const yaml = require('js-yaml');

// Re-exec once with a big heap (the 280 MB dump + parsed objects need it).
if (!process.env._TC_COV_REEXEC) {
    const r = spawnSync(process.execPath,
        ['--max-old-space-size=8192', __filename, ...process.argv.slice(2)],
        { stdio: 'inherit', env: { ...process.env, _TC_COV_REEXEC: '1' } });
    process.exit(r.status === null ? 1 : r.status);
}

const ROOT = path.join(__dirname, '../../..');          // .../TrussC (outer)
const INCLUDE = path.join(ROOT, 'trussc/core/include');
const API_YAML = path.join(__dirname, '../api-definition.yaml');

const argv = process.argv.slice(2);
const argVal = (flag) => { const i = argv.indexOf(flag); return i >= 0 ? argv[i + 1] : null; };

// ---------------------------------------------------------------------------
// 1. AST dump
// ---------------------------------------------------------------------------
function defaultFlags() {
    return [
        '-x', 'objective-c++', '-std=gnu++20', '-DSOKOL_METAL',
        '-isystem', INCLUDE, '-isystem', path.join(INCLUDE, 'sokol'),
        '-arch', 'arm64', '-mmacosx-version-min=14.0', '-fobjc-arc',
    ];
}
function dumpAst() {
    const tu = path.join(os.tmpdir(), 'tc_cov_tu.mm');
    fs.writeFileSync(tu, '#include <TrussC.h>\nint main(){return 0;}\n');
    const extra = process.env.TC_COVERAGE_CXXFLAGS
        ? process.env.TC_COVERAGE_CXXFLAGS.split(/\s+/).filter(Boolean)
        : defaultFlags();
    const flags = [
        ...extra,
        '-Xclang', '-ast-dump=json', '-Xclang', '-ast-dump-filter=trussc',
        '-fsyntax-only', tu,
    ];
    return execFileSync('c++', flags, { maxBuffer: 1024 * 1024 * 1024 }).toString();
}

let astText;
const cached = argVal('--ast');
if (cached) {
    astText = fs.readFileSync(cached, 'utf8');
} else {
    process.stderr.write('coverage-audit: running clang AST dump (~280 MB, a few seconds)…\n');
    astText = dumpAst();
}
const saveTo = argVal('--save-ast');
if (saveTo) { fs.writeFileSync(saveTo, astText); process.stderr.write(`AST dump written to ${saveTo}\n`); process.exit(0); }

// ---------------------------------------------------------------------------
// 2. split top-level JSON objects (clang concatenates one per -filter match)
// ---------------------------------------------------------------------------
function splitTopLevel(s) {
    const objs = [];
    let i = 0;
    const n = s.length;
    while (i < n) {
        while (i < n && /\s/.test(s[i])) i++;
        if (i >= n) break;
        if (s[i] !== '{') { i++; continue; }
        const start = i;
        let depth = 0, inStr = false, esc = false;
        for (; i < n; i++) {
            const c = s[i];
            if (inStr) { if (esc) esc = false; else if (c === '\\') esc = true; else if (c === '"') inStr = false; continue; }
            if (c === '"') { inStr = true; continue; }
            if (c === '{') depth++;
            else if (c === '}') { depth--; if (depth === 0) { i++; break; } }
        }
        objs.push(s.slice(start, i));
    }
    return objs;
}

// ---------------------------------------------------------------------------
// 3. enumerate symbols from the AST (NamespaceDecl / CXXRecordDecl walk)
// ---------------------------------------------------------------------------
function enumerate(objs) {
    const syms = [];
    let sticky = '(unknown)';
    const fileOf = (node) => { if (node.loc && node.loc.file) sticky = node.loc.file; return sticky; };

    function walkRecord(rec, nsPath, extraFlags) {
        const recFile = fileOf(rec);
        syms.push({ kind: 'type', ns: nsPath, owner: null, name: rec.name, file: recFile, flags: [...(extraFlags || [])] });
        let access = rec.tagUsed === 'class' ? 'private' : 'public';
        for (const m of (rec.inner || [])) {
            fileOf(m);
            if (m.kind === 'AccessSpecDecl') { access = m.access; continue; }
            if (m.kind === 'CXXMethodDecl') {
                const flags = [m.isImplicit ? 'implicit' : null, /operator/.test(m.name || '') ? 'operator' : null,
                    m.storageClass === 'static' ? 'static' : null, m.explicitlyDeleted ? 'deleted' : null,
                    m.explicitlyDefaulted ? 'defaulted' : null].filter(Boolean);
                syms.push({ kind: 'method', ns: nsPath, owner: rec.name, name: m.name, sig: m.type && m.type.qualType, file: fileOf(m), access, flags: [...(extraFlags || []), ...flags] });
            } else if (m.kind === 'CXXConstructorDecl') {
                syms.push({ kind: 'ctor', ns: nsPath, owner: rec.name, name: rec.name, file: fileOf(m), access, flags: [m.isImplicit ? 'implicit' : null].filter(Boolean) });
            } else if (m.kind === 'CXXDestructorDecl') {
                syms.push({ kind: 'dtor', ns: nsPath, owner: rec.name, name: '~' + rec.name, file: fileOf(m), access, flags: [] });
            } else if (m.kind === 'FieldDecl') {
                syms.push({ kind: 'field', ns: nsPath, owner: rec.name, name: m.name, file: fileOf(m), access, flags: [] });
            } else if (m.kind === 'EnumDecl' && m.name) {
                syms.push({ kind: 'enum', ns: nsPath, owner: rec.name, name: m.name, file: fileOf(m), access, flags: ['nested'] });
            }
        }
    }
    function walkNamespace(node, nsPath) {
        fileOf(node);
        const name = node.name || '(anon)';
        const p = nsPath ? nsPath + '::' + name : name;
        for (const c of (node.inner || [])) {
            fileOf(c);
            if (c.kind === 'NamespaceDecl') { walkNamespace(c, p); continue; }
            if (c.kind === 'FunctionDecl') {
                syms.push({ kind: 'func', ns: p, owner: null, name: c.name, sig: c.type && c.type.qualType, file: fileOf(c),
                    flags: [c.isImplicit ? 'implicit' : null, /operator/.test(c.name || '') ? 'operator' : null].filter(Boolean) });
            } else if (c.kind === 'CXXRecordDecl' && c.name) {
                walkRecord(c, p);
            } else if (c.kind === 'EnumDecl' && c.name) {
                syms.push({ kind: 'enum', ns: p, owner: null, name: c.name, file: fileOf(c), flags: [] });
            } else if (c.kind === 'ClassTemplateDecl' && c.name) {
                const rec = (c.inner || []).find(x => x.kind === 'CXXRecordDecl');
                if (rec) { rec.name = c.name; walkRecord(rec, p, ['template']); }
            } else if (c.kind === 'FunctionTemplateDecl' && c.name) {
                const fn = (c.inner || []).find(x => x.kind === 'FunctionDecl');
                syms.push({ kind: 'func', ns: p, owner: null, name: c.name, sig: fn && fn.type && fn.type.qualType, file: fileOf(c), flags: ['template'] });
            } else if (c.kind === 'TypedefDecl' || c.kind === 'TypeAliasDecl') {
                syms.push({ kind: 'typedef', ns: p, owner: null, name: c.name, file: fileOf(c), flags: [] });
            } else if (c.kind === 'VarDecl') {
                syms.push({ kind: 'var', ns: p, owner: null, name: c.name, file: fileOf(c), flags: [] });
            }
        }
    }
    for (const o of objs) {
        let node; try { node = JSON.parse(o); } catch (e) { continue; }
        if (node.kind === 'NamespaceDecl') walkNamespace(node, '');
    }
    return syms;
}

// ---------------------------------------------------------------------------
// 4. load yaml + build documented sets
// ---------------------------------------------------------------------------
const api = yaml.load(fs.readFileSync(API_YAML, 'utf8'));
const EXCLUDED = (api.coverage && api.coverage.excluded_namespaces) || [];

const docFree = new Set();          // free function names
const docMethod = new Set();        // "Owner::name"
const docType = new Set();          // type / enum names
const docEnum = new Set();
const docField = new Set();         // "Owner::name"
const docConst = new Set();         // constant / color names
const docOpMember = new Set();      // "Owner::operator<sym>"
const docOpFree = new Set();        // "operator<sym>"
const symToOp = (s) => 'operator' + s;

for (const c of api.categories || []) for (const f of c.functions || []) {
    if (f.self) docMethod.add(f.self + '::' + (f.cppName || f.name));
    else docFree.add(f.cppName || f.name);
}
for (const t of api.types || []) {
    docType.add(t.name);
    for (const m of t.methods || []) docMethod.add(t.name + '::' + (m.cppName || m.name));
    for (const m of t.static_methods || []) docMethod.add(t.name + '::' + (m.cppName || m.name));
    for (const p of t.properties || []) docField.add(t.name + '::' + (p.cppName || p.name));
    for (const o of t.operators || []) docOpMember.add(t.name + '::' + symToOp(o.symbol));
    for (const o of t.free_operators || []) docOpFree.add(symToOp(o.symbol));
}
for (const e of api.enums || []) {
    docEnum.add(e.name); docType.add(e.name);
    for (const o of e.operators || []) docOpFree.add(symToOp(o.symbol));
}
for (const c of api.constants || []) docConst.add(c.cppName || c.name);

// ---------------------------------------------------------------------------
// 5. exclusion + noise rules
// ---------------------------------------------------------------------------
const isTc = (f) => /core\/include\/(tc\/|tc[A-Z]\w*\.h|TrussC\.h)/.test(f || '');
const nsSegs = (ns) => (ns || '').split('::').filter((x) => x && x !== 'trussc' && x !== '(anon)');
const inExcludedNs = (ns) => nsSegs(ns).some((seg) => EXCLUDED.includes(seg));

function isNoise(s) {
    const fl = s.flags || [];
    if (fl.includes('implicit') || fl.includes('defaulted') || fl.includes('deleted') || fl.includes('template')) return true;
    if (s.kind === 'ctor' || s.kind === 'dtor') return true;
    if ((s.name || '').endsWith('_')) return true;          // trailing-underscore convention
    if (s.access && s.access !== 'public') return true;     // non-public members
    if (/tcReflect\.h|tcJsonReflect\.h/.test(s.file || '')) return true;  // reflection machinery
    return false;
}

// ---------------------------------------------------------------------------
// 6. documented? per kind (operators matched against the operator sets)
// ---------------------------------------------------------------------------
function documented(s) {
    if (s.kind === 'func') {
        if (/^operator/.test(s.name)) return docOpFree.has(s.name);
        return docFree.has(s.name) || docMethod.has((s.owner || '') + '::' + s.name) || docConst.has(s.name);
    }
    if (s.kind === 'method') {
        if (/^operator/.test(s.name)) return docOpMember.has(s.owner + '::' + s.name) || s.name === 'operator=';
        return docMethod.has(s.owner + '::' + s.name);
    }
    if (s.kind === 'type') return docType.has(s.name);
    if (s.kind === 'enum') return docEnum.has(s.name) || docType.has(s.name) || docEnum.has((s.owner || '') + '::' + s.name);
    if (s.kind === 'field') return docField.has((s.owner || '') + '::' + s.name);
    if (s.kind === 'typedef') return docType.has(s.name);
    if (s.kind === 'var') return docConst.has(s.name) || docFree.has(s.name);
    return false;
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------
const objs = splitTopLevel(astText);
const all = enumerate(objs);
const tc = all.filter((s) => isTc(s.file));
const afterNs = tc.filter((s) => !inExcludedNs(s.ns));
const removedByNs = tc.length - afterNs.length;
const candidates = afterNs.filter((s) => !isNoise(s));
const residual = candidates.filter((s) => !documented(s));

if (argv.includes('--json')) {
    console.log(JSON.stringify(residual.map((s) => ({ kind: s.kind, owner: s.owner, name: s.name, ns: s.ns, file: (s.file || '').replace(INCLUDE + '/', '') })), null, 2));
    process.exit(0);
}

// namespace-exclusion breakdown
const exclBreak = {};
for (const s of tc) if (inExcludedNs(s.ns)) { const seg = nsSegs(s.ns).find((x) => EXCLUDED.includes(x)); exclBreak[seg] = (exclBreak[seg] || 0) + 1; }
const byKind = {}; for (const s of residual) byKind[s.kind] = (byKind[s.kind] || 0) + 1;

console.log(`\n=== coverage audit (AST) ===`);
console.log(`tc/ public symbols           : ${tc.length}`);
console.log(`excluded by namespace        : ${removedByNs}   ${JSON.stringify(exclBreak)}`);
console.log(`candidate public surface     : ${candidates.length}`);
console.log(`documented                   : ${candidates.length - residual.length}`);
console.log(`UNDOCUMENTED residual        : ${residual.length}   ${JSON.stringify(byKind)}\n`);

// top residual owners
const byOwner = {};
for (const s of residual) { const k = s.owner || ('(free ' + s.kind + ')'); byOwner[k] = (byOwner[k] || 0) + 1; }
console.log('top residual owners:');
for (const [o, n] of Object.entries(byOwner).sort((a, b) => b[1] - a[1]).slice(0, 25)) console.log(`  ${String(n).padStart(3)}  ${o}`);

// known-gap sanity check
const KNOWN = ['logWarning', 'logError', 'fft', 'getBlendModeName', 'Xml', 'Json', 'StrokeMesh', 'BlendMode', 'getRootNode'];
const docAny = (n) => docFree.has(n) || docType.has(n) || docEnum.has(n);
console.log('\nknown-symbol documented check: ' + KNOWN.map((g) => `${g}=${docAny(g) ? '✓' : '✗'}`).join('  '));
console.log('(report-only — residual mixes real doc gaps with intentional-but-public internals)\n');
