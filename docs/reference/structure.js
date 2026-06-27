#!/usr/bin/env node
// ============================================================================
// structure.js — derive the canonical API STRUCTURE from the C++ AST.
//
// Part of the reference redesign (see ../REFERENCE_REDESIGN.md). The structure
// (types, methods, signatures, static/const, deprecated, owner, overloads) is
// DERIVED, never hand-authored — so it cannot drift, duplicate, or lose its
// owner. Prose lives separately in the TOML sidecar, joined by symbol-id.
//
//   node structure.js                      # run clang, print summary
//   node structure.js --json <file>        # write the structure map to <file>
//   node structure.js --ast <file>         # reuse a cached AST dump (skip clang)
//   node structure.js --save-ast <file>    # dump the AST and exit
//   node structure.js --ids                # print just the sorted symbol-ids
//
// Env: $CXX (default c++), $TC_COVERAGE_CXXFLAGS for non-mac include paths.
// ============================================================================
const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const ROOT = path.resolve(__dirname, '../..');                 // trussc/
const INCLUDE = path.join(ROOT, 'core/include');
const argv = process.argv.slice(2);
const argVal = (f) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : null; };

// --- 1. obtain the AST dump (cached or fresh) -------------------------------
function dumpAst() {
    const CXX = process.env.CXX || 'c++';
    const extra = (process.env.TC_COVERAGE_CXXFLAGS || '').split(/\s+/).filter(Boolean);
    const tmp = path.join(require('os').tmpdir(), 'tc-structure-probe.cpp');
    fs.writeFileSync(tmp, '#include <TrussC.h>\n');
    const args = ['-std=c++20', '-DNDEBUG', '-I', INCLUDE, '-Xclang', '-ast-dump=json',
        '-Xclang', '-ast-dump-filter=trussc', '-fsyntax-only', ...extra, tmp];
    process.stderr.write('structure: running clang AST dump (~280 MB, a few seconds)…\n');
    return execSync(`${CXX} ${args.map(a => `'${a}'`).join(' ')}`, { maxBuffer: 1024 * 1024 * 1024 }).toString();
}
const cached = argVal('--ast');
let astText = cached ? fs.readFileSync(cached, 'utf8') : dumpAst();
const saveTo = argVal('--save-ast');
if (saveTo) { fs.writeFileSync(saveTo, astText); process.stderr.write(`AST dump written to ${saveTo}\n`); process.exit(0); }

// --- 2. split + enumerate ---------------------------------------------------
function splitTopLevel(s) {
    const objs = []; let i = 0; const n = s.length;
    while (i < n) {
        while (i < n && /\s/.test(s[i])) i++;
        if (i >= n) break;
        if (s[i] !== '{') { i++; continue; }
        const start = i; let depth = 0, inStr = false, esc = false;
        for (; i < n; i++) {
            const c = s[i];
            if (inStr) { if (esc) esc = false; else if (c === '\\') esc = true; else if (c === '"') inStr = false; continue; }
            if (c === '"') { inStr = true; continue; }
            if (c === '{') depth++; else if (c === '}') { depth--; if (depth === 0) { i++; break; } }
        }
        objs.push(s.slice(start, i));
    }
    return objs;
}
const deprecatedOf = (node) => {
    const a = (node.inner || []).find(x => x.kind === 'DeprecatedAttr');
    return a ? { reason: a.message || '' } : null;
};
const tparamsOf = (node) => (node.inner || [])
    .filter(x => x.kind === 'TemplateTypeParmDecl' || x.kind === 'NonTypeTemplateParmDecl')
    .map(x => x.name || 'T');
// param names come from ParmVarDecl children (the qualType carries types only).
// "const Vec2 &" + name "v" -> "const Vec2 & v"; a ParmVarDecl with a default
// expr child gets a trailing " = …" marker (value not reconstructed).
const paramsOf = (node) => (node.inner || [])
    .filter(x => x.kind === 'ParmVarDecl')
    .map(x => {
        const t = (x.type && x.type.qualType) || 'auto';
        const hasDefault = (x.inner || []).some(c => /Expr|Literal|InitListExpr/.test(c.kind || ''));
        return (x.name ? `${t} ${x.name}` : t) + (hasDefault ? ' = …' : '');
    }).join(', ');

// enumerator names of an EnumDecl (the values: BlendMode { Alpha, Add, ... })
function enumMembersOf(en) {
    return (en.inner || []).filter(x => x.kind === 'EnumConstantDecl' && x.name).map(x => x.name);
}

// TC_* annotations on a decl. Clang's JSON AST omits the annotate STRING, so we
// recover it from the source via each AnnotateAttr's range. (file = the decl's
// source file; same file as the attribute, which sits just before the decl.)
const _srcCache = {};
const _readSrc = (f) => { try { return f in _srcCache ? _srcCache[f] : (_srcCache[f] = fs.readFileSync(f, 'utf8')); } catch (e) { return (_srcCache[f] = ''); } };
const _csv = (s) => s.split(',').map(x => x.trim()).filter(Boolean);
function annotationsOf(node, file) {
    const out = {};
    if (!file) return out;
    for (const a of (node.inner || [])) {
        if (a.kind !== 'AnnotateAttr' || !a.range || !a.range.begin) continue;
        const beg = a.range.begin;                               // direct offset (raw attr) OR expansionLoc (macro use)
        const off = beg.offset != null ? beg.offset : (beg.expansionLoc && beg.expansionLoc.offset);
        if (off == null) continue;
        const win = _readSrc(file).slice(off, off + 240);        // read the source spelling forward
        let m;
        if (/^\s*TC_INTERNAL\b/.test(win) || /annotate\(\s*"tc:internal"/.test(win)) out.internal = true;
        else if (m = win.match(/^\s*TC_PLATFORMS\(\s*"([^"]*)"/) || win.match(/annotate\(\s*"tc:platforms:([^"]*)"/)) out.platforms = _csv(m[1]);
        else if (m = win.match(/^\s*TC_LUA_BIND\(\s*"([^"]*)"/) || win.match(/annotate\(\s*"tc:lua_bind:([^"]*)"/)) out.lua_bind = _csv(m[1]);
    }
    return out;
}
function enumerate(objs) {
    const syms = []; let sticky = '(unknown)';
    const fileOf = (node) => { if (node.loc && node.loc.file) sticky = node.loc.file; return sticky; };
    function walkRecord(rec, nsPath, extraFlags, tps) {
        const recFile = fileOf(rec);
        syms.push({ kind: 'type', ns: nsPath, owner: null, name: rec.name, file: recFile, flags: [...(extraFlags || [])], tparams: tps, ann: annotationsOf(rec, recFile), deprecated: deprecatedOf(rec) });
        let access = rec.tagUsed === 'class' ? 'private' : 'public';
        for (const m of (rec.inner || [])) {
            fileOf(m);
            if (m.kind === 'AccessSpecDecl') { access = m.access; continue; }
            if (m.kind === 'CXXMethodDecl') {
                const flags = [/operator/.test(m.name || '') ? 'operator' : null, m.storageClass === 'static' ? 'static' : null,
                    m.isImplicit ? 'implicit' : null, m.explicitlyDeleted ? 'deleted' : null, m.explicitlyDefaulted ? 'defaulted' : null].filter(Boolean);
                syms.push({ kind: 'method', ns: nsPath, owner: rec.name, name: m.name, sig: m.type && m.type.qualType, params: paramsOf(m), file: fileOf(m), access, flags: [...(extraFlags || []), ...flags], ann: annotationsOf(m, fileOf(m)), deprecated: deprecatedOf(m) });
            } else if (m.kind === 'FieldDecl') {
                syms.push({ kind: 'field', ns: nsPath, owner: rec.name, name: m.name, file: fileOf(m), access, flags: [], deprecated: deprecatedOf(m) });
            } else if (m.kind === 'EnumDecl' && m.name) {
                syms.push({ kind: 'enum', ns: nsPath, owner: rec.name, name: m.name, file: fileOf(m), access, flags: ['nested'], members: enumMembersOf(m), ann: annotationsOf(m, fileOf(m)), deprecated: deprecatedOf(m) });
            } else if (m.kind === 'CXXRecordDecl' && m.name && access === 'public') {
                walkRecord(m, nsPath, extraFlags);              // nested public type (e.g. Node::HitResult) — keyed by its bare name
            }
        }
    }
    function walkNamespace(node, nsPath) {
        fileOf(node);
        const p = nsPath ? nsPath + '::' + (node.name || '(anon)') : (node.name || '(anon)');
        for (const c of (node.inner || [])) {
            fileOf(c);
            if (c.kind === 'NamespaceDecl') { walkNamespace(c, p); continue; }
            if (c.kind === 'FunctionDecl') {
                syms.push({ kind: 'func', ns: p, owner: null, name: c.name, sig: c.type && c.type.qualType, params: paramsOf(c), file: fileOf(c),
                    flags: [/operator/.test(c.name || '') ? 'operator' : null].filter(Boolean), ann: annotationsOf(c, fileOf(c)), deprecated: deprecatedOf(c) });
            } else if (c.kind === 'CXXRecordDecl' && c.name) { walkRecord(c, p);
            } else if (c.kind === 'EnumDecl' && c.name) { syms.push({ kind: 'enum', ns: p, owner: null, name: c.name, file: fileOf(c), flags: [], members: enumMembersOf(c), ann: annotationsOf(c, fileOf(c)), deprecated: deprecatedOf(c) });
            } else if (c.kind === 'ClassTemplateDecl' && c.name) {
                const rec = (c.inner || []).find(x => x.kind === 'CXXRecordDecl');
                if (rec) { rec.name = c.name; walkRecord(rec, p, ['template'], tparamsOf(c)); }
            } else if (c.kind === 'FunctionTemplateDecl' && c.name) {
                const fn = (c.inner || []).find(x => x.kind === 'FunctionDecl');
                syms.push({ kind: 'func', ns: p, owner: null, name: c.name, sig: fn && fn.type && fn.type.qualType, params: fn ? paramsOf(fn) : '', file: fileOf(c), flags: ['template'], tparams: tparamsOf(c), deprecated: deprecatedOf(c) });
            } else if (c.kind === 'TypedefDecl' || c.kind === 'TypeAliasDecl') { syms.push({ kind: 'typedef', ns: p, owner: null, name: c.name, file: fileOf(c), flags: [], deprecated: deprecatedOf(c) });
            } else if (c.kind === 'VarDecl') { syms.push({ kind: 'var', ns: p, owner: null, name: c.name, file: fileOf(c), flags: [], deprecated: deprecatedOf(c) });
            }
        }
    }
    for (const o of objs) { let node; try { node = JSON.parse(o); } catch (e) { continue; } if (node.kind === 'NamespaceDecl') walkNamespace(node, ''); }
    return syms;
}

// --- 3. visibility + symbol-id grammar --------------------------------------
const HIDDEN = ['internal', 'mcp', 'headless', 'hot_reload', 'console'];
const nsSegs = (ns) => (ns || '').split('::').filter(x => x && x !== 'trussc' && x !== '(anon)');
const isHidden = (s) => (s.ann && s.ann.internal) || nsSegs(s.ns).some(seg => HIDDEN.includes(seg));   // TC_INTERNAL or hidden namespace
const nsPrefix = (s) => nsSegs(s.ns).join('::');
const isTc = (f) => /core\/include\/(tc\/|tc[A-Z]\w*\.h|TrussC\.h)/.test(f || '');
const noise = (s) => (s.flags || []).some(f => ['implicit', 'defaulted', 'deleted'].includes(f))
    || (s.name || '').endsWith('_') || s.access === 'private'   // protected = part of the subclass/override contract (Mod hooks, Node::callAfter), kept
    || s.kind === 'ctor' || s.kind === 'dtor' || s.name === 'reflectMembers';
function symbolId(s) {
    const pre = nsPrefix(s); const q = (n) => pre ? pre + '::' + n : n;
    switch (s.kind) {
        case 'method': case 'field': return s.owner + '::' + s.name;
        case 'enum': return s.owner ? s.owner + '::' + s.name : q(s.name);
        case 'type': { const b = q(s.name); return s.tparams && s.tparams.length ? `${b}<${s.tparams.join(', ')}>` : b; }
        case 'func': case 'var': case 'typedef': return q(s.name);
        default: return null;
    }
}
// parse a clang qualType "Ret (params) const" into {ret, params, const}
function parseSig(sig) {
    if (!sig) return null;
    const i = sig.indexOf('(');
    if (i < 0) return { ret: sig.trim(), params: '', const: false };
    let d = 0, end = -1;
    for (let j = i; j < sig.length; j++) { if (sig[j] === '(') d++; else if (sig[j] === ')') { if (--d === 0) { end = j; break; } } }
    return { ret: sig.slice(0, i).trim(), params: sig.slice(i + 1, end).trim(), const: /\bconst\b/.test(sig.slice(end + 1)) };
}

// --- 4. build the structure map (overloads merged into one entry) -----------
const syms = enumerate(splitTopLevel(astText));
const pub = syms.filter(s => isTc(s.file) && !isHidden(s) && !noise(s) && symbolId(s));
const structure = Object.create(null);                          // null proto: ids like "toString"/"constructor" are safe keys
for (const s of pub) {
    const id = symbolId(s);
    if (!structure[id]) structure[id] = { id, kind: s.kind, owner: s.owner || undefined, name: s.name, ns: nsPrefix(s) || undefined, signatures: [], static: false, members: s.members && s.members.length ? s.members : undefined, platforms: s.ann && s.ann.platforms, lua_bind: s.ann && s.ann.lua_bind, deprecated: s.deprecated || undefined, tparams: s.tparams && s.tparams.length ? s.tparams : undefined };
    const e = structure[id];
    if ((s.flags || []).includes('static')) e.static = true;
    if (s.deprecated && !e.deprecated) e.deprecated = s.deprecated;
    if (s.sig) {
        const p = parseSig(s.sig);                             // ret + const from qualType
        if (p) { const sig = { ret: p.ret, params: s.params !== undefined ? s.params : p.params, const: p.const };   // params (named) from ParmVarDecl
            if (!e.signatures.some(x => x.params === sig.params && x.const === sig.const)) e.signatures.push(sig); }
    }
}

// --- 4b. merge curated std::-provided symbols (AST-invisible: sin/cos/lerp …) --
for (const s of require('./std-symbols.js')) {
    if (structure[s.id]) continue;                              // a real trussc:: symbol wins
    structure[s.id] = { id: s.id, kind: s.kind, owner: undefined, name: s.name, ns: undefined,
        signatures: s.signatures || [], static: false, provider: s.provider };
}

// --- 5. output --------------------------------------------------------------
const jsonOut = argVal('--json');
if (argv.includes('--ids')) { console.log(Object.keys(structure).sort().join('\n')); process.exit(0); }
if (jsonOut) { fs.writeFileSync(jsonOut, JSON.stringify(structure, null, 0)); process.stderr.write(`structure written to ${jsonOut} (${Object.keys(structure).length} symbols)\n`); process.exit(0); }

const byKind = {}; for (const id in structure) byKind[structure[id].kind] = (byKind[structure[id].kind] || 0) + 1;
const deprecated = Object.values(structure).filter(e => e.deprecated).length;
console.log('\n=== API structure (from AST) ===');
console.log(`public decls scanned : ${pub.length}`);
console.log(`unique symbols (ids) : ${Object.keys(structure).length}   ${JSON.stringify(byKind)}`);
console.log(`deprecated symbols   : ${deprecated}`);
console.log(`(overloads merged: ${pub.length - Object.keys(structure).length} extra decls collapsed)`);
