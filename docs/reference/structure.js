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
// The dump needs CLANG specifically (-Xclang -ast-dump=json); g++ rejects those
// flags outright. On macOS `c++` IS clang, so defaulting to it worked there and
// died on Linux with four "unrecognized command-line option" lines from g++ --
// the same class of macOS-only assumption as the SOKOL backend define below.
// Probe for a real clang instead, and keep $CXX as the override.
function pickCxx() {
    if (process.env.CXX) return process.env.CXX;
    for (const c of ['clang++', 'c++']) {
        try {
            if (/clang/i.test(execSync(`${c} --version`, { stdio: ['ignore', 'pipe', 'ignore'] }).toString())) return c;
        } catch { /* not installed — try the next candidate */ }
    }
    return 'c++';   // none found: fall through so the failure path reports why
}
function dumpFailure(cause) {
    process.stderr.write(`structure: AST dump could not be read/parsed: ${cause}\n`);
    process.exit(1);
}
function dumpAst() {
    const CXX = pickCxx();
    const extra = (process.env.TC_COVERAGE_CXXFLAGS || '').split(/\s+/).filter(Boolean);
    const tmp = path.join(require('os').tmpdir(), 'tc-structure-probe.cpp');
    fs.writeFileSync(tmp, '#include <TrussC.h>\n');
    // Mirror the backend macro the real build defines for this platform
    // (core/CMakeLists.txt). Headers branch on it — the desktop-Linux guard for
    // the createWindow() unsupported-platform stub is
    // `!(defined(__linux__) && defined(SOKOL_GLCORE))` — so dumping without it
    // makes Linux look like a platform without secondary windows and pulls the
    // stub into the AST alongside the real declaration. That produced a
    // duplicate createWindow overload whose only trace was an extra `[+1]` in
    // the generated index. Override with TC_BACKEND_DEFINE.
    const backend = process.env.TC_BACKEND_DEFINE || (
        process.platform === 'darwin' ? 'SOKOL_METAL'
        : process.platform === 'win32' ? 'SOKOL_D3D11'
        : 'SOKOL_GLCORE');
    const args = ['-std=c++20', '-DNDEBUG', `-D${backend}`, '-I', INCLUDE, '-Xclang', '-ast-dump=json',
        '-Xclang', '-ast-dump-filter=trussc', '-fsyntax-only', ...extra, tmp];
    process.stderr.write('structure: running clang AST dump…\n');
    // Bound (local tooling): capture at most 1 GiB of compiler output; a larger dump fails with an explicit message.
    const maxBuffer = 1024 * 1024 * 1024;
    try {
        return execSync(`${CXX} ${args.map(a => `'${a}'`).join(' ')}`, { maxBuffer });
    } catch (e) {
        // Even a large AST can be truncated by a fatal diagnostic. Never let
        // check.js or an emitter consume that incomplete API surface.
        const err = (e.stderr || '').toString();
        if (e.code === 'ENOBUFS') dumpFailure(`clang output exceeded the ${maxBuffer}-byte capture limit (maxBuffer in structure.js)`);
        if (e.code || (e.status == null && !err)) dumpFailure(e.message || String(e));
        process.stderr.write(`structure: clang AST dump FAILED using CXX=${CXX}: ${e.message || `exit status ${e.status}`}\n`);
        if (/fatal error:/.test(err)) {
            process.stderr.write('structure: hint: core/include/tc/gpu/shaders/*.glsl.h may be missing; generate the shaders first with docs/reference/regenerate.sh --shaders-only.\n');
        }
        if (/unrecognized command-line option/.test(err)) {
            process.stderr.write('structure: that compiler is not clang -- this dump requires clang (-Xclang -ast-dump=json).\n'
                + 'structure: install clang or set CXX=/path/to/clang++.\n');
        }
        if (err) process.stderr.write('structure: compiler stderr:\n' + err + '\n');
        process.exit(1);
    }
}
const cached = argVal('--ast');
let astBuffer;
if (cached) {
    try { astBuffer = fs.readFileSync(cached); }
    catch (e) { dumpFailure(`${cached}: ${e.message}`); }
} else { astBuffer = dumpAst(); }
const saveTo = argVal('--save-ast');
if (saveTo) { fs.writeFileSync(saveTo, astBuffer); process.stderr.write(`AST dump written to ${saveTo}\n`); process.exit(0); }

// --- 2. split + enumerate ---------------------------------------------------
// Scan bytes, not one giant JS string: the full dump can exceed V8's string
// limit, while each namespace object is much smaller. Only decode one at a time.
function* splitTopLevel(s) {
    let i = 0; const n = s.length;
    let count = 0;
    while (i < n) {
        while (i < n && (s[i] === 32 || s[i] === 9 || s[i] === 10 || s[i] === 13)) i++;
        if (i >= n) break;
        if (s[i] !== 123) dumpFailure(`expected '{' at byte ${i}`);
        const start = i; let depth = 0, inStr = false, esc = false;
        for (; i < n; i++) {
            const c = s[i];
            if (inStr) { if (esc) esc = false; else if (c === 92) esc = true; else if (c === 34) inStr = false; continue; }
            if (c === 34) { inStr = true; continue; }
            if (c === 123) depth++; else if (c === 125) { depth--; if (depth === 0) { i++; break; } }
        }
        if (depth !== 0) dumpFailure(`unterminated object starting at byte ${start} (end at byte ${n})`);
        let node;
        try { node = JSON.parse(s.subarray(start, i).toString('utf8')); }
        catch (e) { dumpFailure(`object starting at byte ${start}: ${e.message}`); }
        count++;
        yield node;
    }
    if (!count) dumpFailure('no top-level JSON objects');
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
const _defExprKind = (k) => /Expr|Literal|InitListExpr/.test(k || '');
const _hasDefault = (x) => (x.inner || []).some(c => _defExprKind(c.kind));
// Reconstruct a parameter's default-argument value from the AST when it is a
// simple constant (literal / enum ref / default-ctor / brace-init); returns null
// for expressions we can't render faithfully, so paramsOf falls back to "= …".
const _fmtFloat = (v) => { const f = parseFloat(v); if (!isFinite(f)) return v; return Number.isInteger(f) ? f.toFixed(1) : String(f); };
const _bareType = (t) => (t || '').replace(/^const\s+/, '').replace(/\s*[&*]+\s*$/, '').trim();
function _evalExpr(e) {
    if (!e) return null;
    const inner1 = () => _evalExpr((e.inner || []).find(c => _defExprKind(c.kind)));
    switch (e.kind) {
        case 'IntegerLiteral': return e.value;
        case 'FloatingLiteral': return _fmtFloat(e.value);
        case 'CXXBoolLiteralExpr': return String(e.value);
        case 'StringLiteral': return e.value;
        case 'CXXNullPtrLiteralExpr': case 'GNUNullExpr': return 'nullptr';
        case 'DeclRefExpr': return (e.referencedDecl && e.referencedDecl.name) || null;
        case 'UnaryOperator': { const v = inner1(); return v != null ? (e.opcode || '') + v : null; }
        case 'CXXConstructExpr': case 'CXXTemporaryObjectExpr': {   // Type() / Type(args) temporaries (WindowSettings(), Color(...))
            const args = (e.inner || []).filter(c => _defExprKind(c.kind) && c.kind !== 'CXXDefaultInitExpr' && c.kind !== 'CXXDefaultArgExpr');
            const tn = _bareType(e.type && e.type.qualType);
            if (!args.length) return tn ? tn + '()' : null;
            const vs = args.map(_evalExpr); return vs.every(v => v != null) ? `${tn}(${vs.join(', ')})` : null;
        }
        case 'InitListExpr': {   // brace init; all-defaulted members (= {}) render as "{}"
            const items = (e.inner || []).filter(c => _defExprKind(c.kind) && c.kind !== 'CXXDefaultInitExpr');
            if (!items.length) return '{}';
            const vs = items.map(_evalExpr); return vs.every(v => v != null) ? `{${vs.join(', ')}}` : null;
        }
        case 'ImplicitCastExpr': case 'CXXStaticCastExpr': case 'ConstantExpr':
        case 'CXXFunctionalCastExpr': case 'MaterializeTemporaryExpr':
        case 'CXXBindTemporaryExpr': case 'ExprWithCleanups': case 'ParenExpr':
        case 'ImplicitValueInitExpr': return inner1();
        default: return null;
    }
}
const _defaultOf = (x) => { const e = (x.inner || []).find(c => _defExprKind(c.kind)); return e ? _evalExpr(e) : null; };
const paramsOf = (node) => (node.inner || [])
    .filter(x => x.kind === 'ParmVarDecl')
    .map(x => {
        const t = (x.type && x.type.qualType) || 'auto';
        const base = x.name ? `${t} ${x.name}` : t;
        if (!_hasDefault(x)) return base;
        const dv = _defaultOf(x);
        return base + (dv != null ? ` = ${dv}` : ' = …');
    }).join(', ');

// structured args for a signature — {type,name,hasDefault,default?} (+ cheap flags
// from the spelled type) for downstream binding generators (tcxLua).
const argsOf = (node) => (node.inner || [])
    .filter(x => x.kind === 'ParmVarDecl')
    .map(x => {
        const type = (x.type && x.type.qualType) || 'auto';
        const a = { type, name: x.name || '', hasDefault: _hasDefault(x) };
        if (a.hasDefault) { const dv = _defaultOf(x); if (dv != null) a.default = dv; }
        if (/&\s*$/.test(type)) a.isRef = true;            // lvalue or rvalue reference
        if (/\bconst\b/.test(type)) a.isConst = true;
        if (/\*\s*$/.test(type)) a.isPointer = true;
        if (/\[/.test(type)) a.isArray = true;
        return a;
    });

// enumerators of an EnumDecl as {name, value}. Explicit values come from the
// ConstantExpr's evaluated value; the rest are positional (prev + 1, from 0).
function enumMembersOf(en) {
    const out = []; let next = 0;
    for (const x of (en.inner || [])) {
        if (x.kind !== 'EnumConstantDecl' || !x.name) continue;
        const ce = (x.inner || []).find(c => c.kind === 'ConstantExpr');
        let v = ce && ce.value != null ? parseInt(ce.value, 10) : next;
        if (Number.isNaN(v)) v = next;
        out.push({ name: x.name, value: v });
        next = v + 1;
    }
    return out;
}

// TC_* annotations on a decl. Clang's JSON AST omits the annotate STRING, so we
// recover it from the source via each AnnotateAttr's range. (file = the decl's
// source file; same file as the attribute, which sits just before the decl.)
const _lineCache = {};
const _lines = (f) => { try { return f in _lineCache ? _lineCache[f] : (_lineCache[f] = fs.readFileSync(f, 'utf8').split('\n')); } catch (e) { return (_lineCache[f] = []); } };
const _csv = (s) => s.split(',').map(x => x.trim()).filter(Boolean);
function annotationsOf(node, file) {
    const out = {};
    if (!file) return out;
    const lines = _lines(file);
    for (const a of (node.inner || [])) {
        if (a.kind !== 'AnnotateAttr' || !a.range || !a.range.begin) continue;
        // clang's JSON AST omits the annotate string AND its offset is unreliable
        // (lands anywhere in the macro token), so recover the spelling by LINE:
        // expansionLoc.line is the line the TC_* macro was written on.
        const beg = a.range.begin;
        const loc = beg.expansionLoc || beg;
        const ln = loc.line;
        if (!ln) continue;
        const text = (lines[ln - 1] || '') + ' ' + (lines[ln] || '');   // macro line (+next, in case the decl wraps)
        // Several TC_* macros can sit on one line (`TC_PLATFORMS("...") TC_LUA_SKIP void f();`),
        // each with its own AnnotateAttr on that line. Read every macro on the macro
        // line itself; only when it has none, fall back to the first one in the
        // two-line window.
        const RX = /\bTC_(PLATFORMS|LUA_BIND|LUA_SKIP)\b(?:\(\s*"([^"]*)")?/g;
        let ms = [...(lines[ln - 1] || '').matchAll(RX)];
        if (!ms.length) { const m1 = text.match(new RegExp(RX.source)); if (m1) ms = [m1]; }
        let m;
        if (ms.length) {
            for (const mm of ms) {
                if (mm[1] === 'PLATFORMS') out.platforms = _csv(mm[2] || '');
                else if (mm[1] === 'LUA_BIND') out.lua_bind = _csv(mm[2] || '');
                else if (mm[1] === 'LUA_SKIP') out.lua_skip = true;
            }
        } else if ((m = text.match(/clang::annotate\(\s*"tc:([^"]*)"/))) {
            const s = m[1];
            if (s.startsWith('platforms:')) out.platforms = _csv(s.slice(10));
            else if (s.startsWith('lua_bind:')) out.lua_bind = _csv(s.slice(9));
            else if (s === 'lua_skip') out.lua_skip = true;
        }
    }
    return out;
}
function enumerate(objs) {
    const syms = []; let sticky = '(unknown)';
    const fileOf = (node) => { if (node.loc && node.loc.file) sticky = node.loc.file; return sticky; };
    function walkRecord(rec, nsPath, extraFlags, tps, enclosing) {
        // skip forward declarations (`struct Color;`) — only the complete
        // definition carries members/constructors. Without this, a forward decl
        // seen before the definition wins the "first id" race and loses the ctors.
        if (rec.completeDefinition !== true) return;
        const recFile = fileOf(rec);
        // qualified owner for members + nested types (Entry -> ChipSoundBundle::Entry)
        const qn = enclosing ? enclosing + '::' + rec.name : rec.name;
        const typeSym = { kind: 'type', ns: nsPath, owner: enclosing || null, name: rec.name, file: recFile, flags: [...(extraFlags || [])], tparams: tps, ann: annotationsOf(rec, recFile), deprecated: deprecatedOf(rec), bases: rec.bases || [] };
        syms.push(typeSym);
        let access = rec.tagUsed === 'class' ? 'private' : 'public';
        for (const m of (rec.inner || [])) {
            fileOf(m);
            if (m.kind === 'AccessSpecDecl') { access = m.access; continue; }
            if (m.kind === 'CXXConstructorDecl' && access === 'public'
                && !m.isImplicit && !m.explicitlyDeleted && !/&&/.test((m.type && m.type.qualType) || '')) {
                const ca = argsOf(m);
                const isCopy = ca.length === 1 && ca[0].type === `const ${rec.name} &`;   // skip copy ctor (noise)
                if (!isCopy) (typeSym.ctors = typeSym.ctors || []).push({ params: paramsOf(m), args: ca, ...annotationsOf(m, fileOf(m)), deprecated: deprecatedOf(m) });
                continue;
            }
            if (m.kind === 'CXXMethodDecl') {
                const flags = [/operator/.test(m.name || '') ? 'operator' : null, m.storageClass === 'static' ? 'static' : null,
                    m.isImplicit ? 'implicit' : null, m.explicitlyDeleted ? 'deleted' : null, m.explicitlyDefaulted ? 'defaulted' : null].filter(Boolean);
                syms.push({ kind: 'method', ns: nsPath, owner: qn, name: m.name, sig: m.type && m.type.qualType, params: paramsOf(m), args: argsOf(m), file: fileOf(m), access, flags: [...(extraFlags || []), ...flags], ann: annotationsOf(m, fileOf(m)), deprecated: deprecatedOf(m) });
            } else if (m.kind === 'FunctionTemplateDecl' && m.name) {
                // A templated member (ToolBuilder::arg<T>, Node::getMod<T>) is a
                // FunctionTemplateDecl wrapping the method, so the CXXMethodDecl
                // branch above never sees it. Read the wrapped decl the way the
                // namespace-level branch below does, and flag it 'template' so
                // the binders skip it — every consumer already filters sig.tmpl.
                const fn = (m.inner || []).find(x => x.kind === 'CXXMethodDecl' || x.kind === 'FunctionDecl');
                if (fn) syms.push({ kind: 'method', ns: nsPath, owner: qn, name: m.name, sig: fn.type && fn.type.qualType, params: paramsOf(fn), args: argsOf(fn), file: fileOf(m), access,
                    flags: [...(extraFlags || []), 'template', /operator/.test(m.name) ? 'operator' : null, fn.storageClass === 'static' ? 'static' : null].filter(Boolean),
                    ann: annotationsOf(fn, fileOf(m)), deprecated: deprecatedOf(fn) });
            } else if (m.kind === 'FieldDecl') {
                syms.push({ kind: 'field', ns: nsPath, owner: qn, name: m.name, ftype: m.type && m.type.qualType, file: fileOf(m), access, flags: [], ann: annotationsOf(m, fileOf(m)), deprecated: deprecatedOf(m) });
            } else if (m.kind === 'EnumDecl' && m.name) {
                syms.push({ kind: 'enum', ns: nsPath, owner: qn, name: m.name, file: fileOf(m), access, flags: ['nested'], members: enumMembersOf(m), ann: annotationsOf(m, fileOf(m)), deprecated: deprecatedOf(m) });
            } else if (m.kind === 'CXXRecordDecl' && m.name && access === 'public') {
                walkRecord(m, nsPath, extraFlags, undefined, qn);   // nested public type -> keyed Owner::Name (Node::HitResult, ChipSoundBundle::Entry)
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
                syms.push({ kind: 'func', ns: p, owner: null, name: c.name, sig: c.type && c.type.qualType, params: paramsOf(c), args: argsOf(c), file: fileOf(c),
                    flags: [/operator/.test(c.name || '') ? 'operator' : null].filter(Boolean), ann: annotationsOf(c, fileOf(c)), deprecated: deprecatedOf(c) });
            } else if (c.kind === 'CXXRecordDecl' && c.name) { walkRecord(c, p);
            } else if (c.kind === 'EnumDecl' && c.name) { syms.push({ kind: 'enum', ns: p, owner: null, name: c.name, file: fileOf(c), flags: [], members: enumMembersOf(c), ann: annotationsOf(c, fileOf(c)), deprecated: deprecatedOf(c) });
            } else if (c.kind === 'ClassTemplateDecl' && c.name) {
                const rec = (c.inner || []).find(x => x.kind === 'CXXRecordDecl');
                if (rec) { rec.name = c.name; walkRecord(rec, p, ['template'], tparamsOf(c)); }
            } else if (c.kind === 'FunctionTemplateDecl' && c.name) {
                const fn = (c.inner || []).find(x => x.kind === 'FunctionDecl');
                // the AnnotateAttr (TC_PLATFORMS / TC_LUA_BIND) lives on the inner FunctionDecl, not the template decl
                syms.push({ kind: 'func', ns: p, owner: null, name: c.name, sig: fn && fn.type && fn.type.qualType, params: fn ? paramsOf(fn) : '', args: fn ? argsOf(fn) : [], file: fileOf(c), flags: ['template'], tparams: tparamsOf(c), ann: fn ? annotationsOf(fn, fileOf(c)) : undefined, deprecated: fn ? deprecatedOf(fn) : deprecatedOf(c) });
            } else if (c.kind === 'TypedefDecl' || c.kind === 'TypeAliasDecl') { syms.push({ kind: 'typedef', ns: p, owner: null, name: c.name, file: fileOf(c), flags: [], deprecated: deprecatedOf(c) });
            } else if (c.kind === 'VarDecl') { syms.push({ kind: 'var', ns: p, owner: null, name: c.name, file: fileOf(c), flags: [], ann: annotationsOf(c, fileOf(c)), deprecated: deprecatedOf(c) });
            }
        }
    }
    for (const node of objs) { if (node.kind === 'NamespaceDecl') walkNamespace(node, ''); }
    return syms;
}

// --- 3. visibility + symbol-id grammar --------------------------------------
const HIDDEN = ['internal', 'headless', 'hot_reload', 'bitmapfont', 'colors'];   // colors:: is rendered as the swatch palette, not the flat symbol list
const nsSegs = (ns) => (ns || '').split('::').filter(x => x && x !== 'trussc' && x !== '(anon)');
const isHidden = (s) => nsSegs(s.ns).some(seg => HIDDEN.includes(seg));   // hidden namespace (internal:: at any depth, …); per-symbol hide is doc-side (`hide = true`)
const nsPrefix = (s) => nsSegs(s.ns).join('::');
const isTc = (f) => /core\/include\/(tc\/|tc[A-Z]\w*\.h|TrussC\.h)/.test(f || '');
const noise = (s) => (s.flags || []).some(f => ['implicit', 'defaulted', 'deleted'].includes(f))
    || (s.name || '').endsWith('_') || s.access === 'private'   // protected kept in the data (carries access; the web emitter hides it, prose preserved for later re-surfacing)
    || s.kind === 'ctor' || s.kind === 'dtor' || s.name === 'reflectMembers';
function symbolId(s) {
    const pre = nsPrefix(s); const q = (n) => pre ? pre + '::' + n : n;
    switch (s.kind) {
        // Members are qualified like everything else: a type in a sub-namespace
        // would otherwise be `mcp::ToolBuilder` while its own method is a bare
        // `ToolBuilder::arg`, and generic member names (`Tool::name`) would read
        // as top-level ids. Keep the feature namespace on public MCP members.
        case 'method': case 'field': return q(s.owner + '::' + s.name);
        case 'enum': return s.owner ? q(s.owner + '::' + s.name) : q(s.name);
        case 'type': { const b = s.owner ? q(s.owner + '::' + s.name) : q(s.name); return s.tparams && s.tparams.length ? `${b}<${s.tparams.join(', ')}>` : b; }
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
const syms = enumerate(splitTopLevel(astBuffer));
let pub = syms.filter(s => isTc(s.file) && !isHidden(s) && !noise(s) && symbolId(s));
// drop OPAQUE enum declarations (`enum class StrokeCap;`) when the same enum is
// also seen complete. An opaque decl carries no enumerators, and it can live in a
// header included EARLIER than the definition (tcWindowContext.h needs StrokeCap
// as a member type before tcRenderContext.h defines it) — so the "first id wins"
// dedupe below would let it beat the definition and publish an enum with zero
// members. Symmetric with walkRecord's completeDefinition guard, but exact: a
// legitimately empty `enum class Foo {};` has no complete-with-members sibling
// and is kept as-is.
const enumsWithMembers = new Set(pub.filter(s => s.kind === 'enum' && s.members && s.members.length).map(symbolId));
pub = pub.filter(s => !(s.kind === 'enum' && !(s.members && s.members.length) && enumsWithMembers.has(symbolId(s))));
// Resolve inheritance before grouping overloads. C++ member lookup hides a base
// name when the derived type declares that name. Constructors are not members
// inherited by this walk; only public members of public bases are copied.
const records = new Map(pub.filter(s => s.kind === 'type').map(s => [
    [nsPrefix(s), s.owner, s.name].filter(Boolean).join('::'), s]));
const ownMembers = new Map();
for (const s of pub.filter(s => s.owner)) {
    const key = [nsPrefix(s), s.owner].filter(Boolean).join('::');
    if (!ownMembers.has(key)) ownMembers.set(key, []);
    ownMembers.get(key).push(s);
}
const declaredNames = new Map();
for (const s of syms.filter(s => s.owner)) {
    const key = [nsPrefix(s), s.owner].filter(Boolean).join('::');
    if (!declaredNames.has(key)) declaredNames.set(key, new Set());
    declaredNames.get(key).add(s.name);
}
const resolvedMembers = new Map();
function membersOf(key) {
    if (resolvedMembers.has(key)) return resolvedMembers.get(key);
    const rec = records.get(key);
    const members = [...(ownMembers.get(key) || [])];
    const names = new Set(declaredNames.get(key));
    for (const base of (rec.bases || [])) {
        if (base.access !== 'public') continue;
        const baseKey = (base.type.desugaredQualType || base.type.qualType).replace(/^trussc::/, '');
        if (!records.has(baseKey)) continue;
        const inherited = membersOf(baseKey).filter(s => (!s.access || s.access === 'public') && !names.has(s.name));
        for (const s of inherited) members.push({ ...s, ctors: s.ctors?.map(c => ({ ...c })), ns: rec.ns,
            owner: [rec.owner, rec.name].filter(Boolean).join('::'),
            declaredIn: s.declaredIn || baseKey });
        for (const s of inherited) names.add(s.name);
    }
    resolvedMembers.set(key, members);
    return members;
}
for (const key of records.keys()) pub.push(...membersOf(key).filter(s => s.declaredIn));

// Every declaration uses its own metadata, otherwise its owner type's. Resolve
// nested owners first, so the same rule applies at every depth.
const resolvedTypes = new Set();
function resolveType(key) {
    const rec = records.get(key);
    if (!rec || resolvedTypes.has(key)) return rec;
    const owner = rec.owner && resolveType([nsPrefix(rec), rec.owner].filter(Boolean).join('::'));
    resolveMetadata(rec, owner);
    resolvedTypes.add(key);
    return rec;
}
function resolveMetadata(s, owner) {
    s.ann = { ...s.ann, platforms: s.ann?.platforms ?? owner?.ann?.platforms ?? null };
    s.deprecated = s.deprecated ?? owner?.deprecated ?? null;
}
for (const key of records.keys()) resolveType(key);
for (const s of pub) {
    const owner = s.owner && records.get([nsPrefix(s), s.owner].filter(Boolean).join('::'));
    resolveMetadata(s, owner);
    for (const ctor of (s.ctors || [])) {
        ctor.platforms = ctor.platforms ?? s.ann.platforms;
        ctor.deprecated = ctor.deprecated ?? s.deprecated;
    }
}
const structure = Object.create(null);                          // null proto: ids like "toString"/"constructor" are safe keys
for (const s of pub) {
    const id = symbolId(s);
    if (!structure[id]) structure[id] = { id, kind: s.kind, owner: s.owner || undefined, declaredIn: s.declaredIn, name: s.name, ns: nsPrefix(s) || undefined, signatures: [], static: false, type: s.ftype || undefined, access: s.access && s.access !== 'public' ? s.access : undefined, members: s.members && s.members.length ? s.members : undefined, constructors: s.ctors && s.ctors.length ? s.ctors : undefined, platforms: s.ann && s.ann.platforms, lua_bind: s.ann && s.ann.lua_bind, lua_skip: (s.ann && s.ann.lua_skip) || undefined, deprecated: s.deprecated, tparams: s.tparams && s.tparams.length ? s.tparams : undefined };
    const e = structure[id];
    if ((s.flags || []).includes('static')) e.static = true;
    if (s.deprecated && !e.deprecated) e.deprecated = s.deprecated;
    if (s.sig) {
        const p = parseSig(s.sig);                             // ret + const from qualType
        if (p) { const sig = { ret: p.ret, params: s.params !== undefined ? s.params : p.params, const: p.const, args: s.args,
                tmpl: (s.flags || []).includes('template') || undefined,     // tmpl: needs explicit instantiation (e.g. typeName<T>()) — binders skip it
                deprecated: s.deprecated };                     // C++ carries [[deprecated]] per DECLARATION, so it belongs on the overload
            if (!e.signatures.some(x => x.params === sig.params && x.const === sig.const)) e.signatures.push(sig); }
    }
}

// --- 4b. merge curated std::-provided symbols (AST-invisible: sin/cos/lerp …) --
for (const s of require('./std-symbols.js')) {
    if (structure[s.id]) continue;                              // a real trussc:: symbol wins
    structure[s.id] = { id: s.id, kind: s.kind, owner: undefined, name: s.name, ns: undefined,
        signatures: s.signatures || [], static: false, provider: s.provider, platforms: null, deprecated: null };
}

// Hot-reload opt-in/opt-out macros have no AST declarations. Keep their cards
// on the reference surface only while the public header defines them.
const hotReloadHeader = fs.readFileSync(path.join(ROOT, 'core/include/tc/app/tcHotReload.h'), 'utf8');
for (const name of ['TC_HOT_RELOAD', 'TC_NO_HOT_RELOAD']) {
    if (!new RegExp(`^#define ${name}\\(`, 'm').test(hotReloadHeader)) continue;
    structure[name] = { id: name, kind: 'macro', name, signatures: [], lua_skip: true };
}

// --- 4c. reconcile per-overload deprecation ---------------------------------
// A symbol's `deprecated` means EVERY overload is deprecated. It used to mean
// "at least one is" (the first deprecated decl won, above), and every consumer
// that skips deprecated symbols therefore dropped the LIVE overload along with
// the dead one. That is not theoretical: adding a replacement overload is the
// house deprecation pattern, and it had already erased Node::globalToLocal(Vec3),
// Node::localToGlobal(Vec3) and all six Event::listen forms from the Lua
// reference while they stayed callable. The distinction now lives on each
// signature; the symbol-level flag is only set when nothing is left to
// recommend. Symbols with no signatures (types, fields, enums) keep the flag
// straight off their declaration.
for (const id in structure) {
    const e = structure[id];
    if (!e.signatures || !e.signatures.length) continue;
    const dep = e.signatures.filter((sig) => sig.deprecated);
    e.deprecated = dep.length === e.signatures.length ? dep[0].deprecated : null;
}

// --- 5. output --------------------------------------------------------------
const jsonOut = argVal('--json');
if (argv.includes('--ids')) { console.log(Object.keys(structure).filter(id => !structure[id].declaredIn || structure[id].declaredIn === structure[id].owner).sort().join('\n')); process.exit(0); }
if (jsonOut) {
    const sorted = Object.fromEntries(Object.keys(structure).sort().map(id => [id, structure[id]]));
    fs.writeFileSync(jsonOut, JSON.stringify(sorted, null, 0));
    process.stderr.write(`structure written to ${jsonOut} (${Object.keys(structure).length} symbols)\n`);
    process.exit(0);
}

const byKind = {}; for (const id in structure) byKind[structure[id].kind] = (byKind[structure[id].kind] || 0) + 1;
const deprecated = Object.values(structure).filter(e => e.deprecated).length;
console.log('\n=== API structure (from AST) ===');
console.log(`public decls scanned : ${pub.length}`);
console.log(`unique symbols (ids) : ${Object.keys(structure).length}   ${JSON.stringify(byKind)}`);
console.log(`deprecated symbols   : ${deprecated}`);
console.log(`(overloads merged: ${pub.length - Object.keys(structure).length} extra decls collapsed)`);
