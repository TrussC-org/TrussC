#!/usr/bin/env node
// Reverse-index scanner: which examples best demonstrate each API symbol.
//
// Usable two ways:
//   - `node scan-examples.js`            -> diagnostic aggregate report (tuning)
//   - require('./scan-examples').buildReverseIndex({funcs, types})  (generator)
//
// Ranking signals: title match (example dir contains the symbol) = top hit,
// then prominence (uses / distinct-APIs-in-example) favours focused demos.
// IDF: symbols used by > threshold of examples are suppressed (setup/clear/...).
// Lifecycle/Events callbacks are excluded (defined, not demonstrated).

const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '../../..');
// Repo-relative: independent of what the framework checkout is named. Deriving it
// from ROOT hardcoded a lowercase 'trussc/', which only resolves on a
// case-insensitive filesystem (macOS) and threw ENOENT on Linux.
const EXAMPLES_DIR = path.join(__dirname, '../../examples');
const API_JS = path.join(ROOT, 'trussc.org/generated/trussc-api.js');

const CALLBACK_CATEGORIES = new Set(['Lifecycle', 'Events']);
const SKIP_DIR = /^(build|bin|_deps|CMakeFiles)$|-(build|src|prefix)$|^box2d/;

function loadSymbols() {
    const api = require(API_JS);
    const funcs = new Set(), types = new Set(), excluded = new Set();
    for (const c of api.categories || []) {
        for (const f of c.functions || []) {
            if (CALLBACK_CATEGORIES.has(c.name)) excluded.add(f.name);
            else funcs.add(f.name);
        }
    }
    for (const name of excluded) funcs.delete(name);
    for (const t of api.types || []) types.add(t.name);
    return { funcs, types, excluded };
}

function findExamples(dir, out = []) {
    for (const name of fs.readdirSync(dir)) {
        if (name.startsWith('.') || SKIP_DIR.test(name)) continue;
        const p = path.join(dir, name);
        if (!fs.statSync(p).isDirectory()) continue;
        if (fs.existsSync(path.join(p, 'src')) && fs.existsSync(path.join(p, 'CMakeLists.txt'))) out.push(p);
        else findExamples(p, out);
    }
    return out;
}

function readSources(exDir) {
    let text = '';
    const walk = (d) => {
        for (const n of fs.readdirSync(d)) {
            const p = path.join(d, n);
            if (fs.statSync(p).isDirectory()) { if (n !== 'build') walk(p); }
            else if (/\.(cpp|h|hpp|cc)$/.test(n)) text += '\n' + fs.readFileSync(p, 'utf8');
        }
    };
    walk(path.join(exDir, 'src'));
    return text;
}

// Core scan: returns { index, dfList, suppressed, excluded, total }
//   index[symbol] = [{ name, group, count, prominence, titleMatch, score }] (ranked, full list)
function buildReverseIndex({ funcs, types, excluded = new Set(), threshold = 0.20 }) {
    const exDirs = findExamples(EXAMPLES_DIR).sort();
    const total = exDirs.length;
    const raw = new Map();                // symbol -> Map(exName -> {count, group, distinct})
    const exMeta = new Map();

    for (const dir of exDirs) {
        const name = path.basename(dir);
        const group = path.basename(path.dirname(dir));
        const code = readSources(dir)
            .replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ').replace(/"(?:[^"\\]|\\.)*"/g, ' ');
        const counts = new Map();
        const bump = (s) => counts.set(s, (counts.get(s) || 0) + 1);
        let m;
        const callRe = /(\.|->)?\s*\b([A-Za-z_]\w*)\s*\(/g;        // calls, excluding member access
        while ((m = callRe.exec(code))) { if (!m[1] && funcs.has(m[2])) bump(m[2]); }
        const wordRe = /\b([A-Za-z_]\w*)\b/g;                      // types by plain word
        while ((m = wordRe.exec(code))) { if (types.has(m[1])) bump(m[1]); }
        exMeta.set(name, { group, distinct: counts.size });
        for (const [sym, c] of counts) {
            if (!raw.has(sym)) raw.set(sym, new Map());
            raw.get(sym).set(name, c);
        }
    }

    const dfList = [...raw.entries()].map(([sym, mp]) => ({ sym, df: mp.size, pct: mp.size / total }))
        .sort((a, b) => b.df - a.df);
    const suppressed = new Set(dfList.filter(d => d.pct > threshold).map(d => d.sym));

    const index = {};
    for (const [sym, mp] of raw) {
        if (suppressed.has(sym)) continue;
        const ranked = [...mp.entries()].map(([name, count]) => {
            const meta = exMeta.get(name);
            const titleMatch = name.toLowerCase().includes(sym.toLowerCase());
            const prominence = count / Math.max(1, meta.distinct);
            return { name, group: meta.group, count, titleMatch,
                prominence: +prominence.toFixed(3),
                score: +((titleMatch ? 1000 : 0) + prominence * 10 + count * 0.01).toFixed(3) };
        }).sort((a, b) => b.score - a.score);
        index[sym] = ranked;
    }
    return { index, dfList, suppressed, excluded, total };
}

// ---- CLI diagnostic ----------------------------------------------------
if (require.main === module) {
    const { funcs, types, excluded } = loadSymbols();
    const r = buildReverseIndex({ funcs, types, excluded });
    console.log(`\n=== ${r.total} examples, ${funcs.size + types.size} symbols (excl ${excluded.size} callbacks) ===\n`);
    console.log('--- TOP 20 by document frequency ---');
    for (const d of r.dfList.slice(0, 20)) console.log(`  ${String(d.df).padStart(3)} ${(d.pct * 100).toFixed(0).padStart(3)}%  ${d.sym}${r.suppressed.has(d.sym) ? '  <suppressed>' : ''}`);
    console.log(`\nsuppressed ${r.suppressed.size} symbols (df% > 20%)\n`);
    console.log('--- sample reverse index (top 3, ★ = title match) ---');
    for (const sym of ['drawRectSquircle', 'EasyCam', 'Fbo', 'drawBox', 'beginShape', 'noise', 'drawRect']) {
        const list = r.index[sym];
        if (!list) { console.log(`\n  ${sym}: (suppressed or unreferenced)`); continue; }
        console.log(`\n  ${sym}`);
        for (const e of list.slice(0, 3)) console.log(`     ${e.titleMatch ? '★' : ' '} ${e.group}/${e.name}  (count ${e.count}, prom ${e.prominence})`);
    }
    console.log('');
}

module.exports = { loadSymbols, buildReverseIndex };
