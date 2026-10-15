#!/usr/bin/env node
// ============================================================================
// emit-of.js — emit the openFrameworks↔TrussC migration guide from the
// canonical reference-data.json (a downstream consumer; see README.md).
//
// The oF mapping is pure PROSE: which oF symbol maps to which tc symbol, with
// migration notes, grouped into display categories. Sources:
//   - reference-data.json : `of` (equivalents), `of_notes`, `of_category`,
//     `category`, name + signature  — all mined into api-reference.toml
//   - of-mapping-config.js: the curated display grouping + a few hand rows
//     (ofOnlyEntries) that have no single api-definition symbol
//
// Outputs (matching the legacy generate-docs.js shapes):
//   - trussc.org/generated/of-mapping.json      (web reference data)
//   - TrussC_vs_openFrameworks.md  §5            (marker-injected table)
//
//   node emit-of.js [--data <ref-data.json>] [--json <out>] [--md <md>] [--dry]
// ============================================================================
const fs = require('fs');
const path = require('path');

const argv = process.argv.slice(2);
const argVal = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const DATA = argVal('--data', path.join(__dirname, 'reference-data.json'));
const OUT_JSON = argVal('--json', path.join(__dirname, '../../../trussc.org/generated/of-mapping.json'));
const OUT_MD = argVal('--md', path.join(__dirname, '../TrussC_vs_openFrameworks.md'));
const START = '<!-- AUTO-GENERATED-START -->';
const END = '<!-- AUTO-GENERATED-END -->';

const { categoryMapping, typeCategoryMapping, ofOnlyEntries } = require('./of-mapping-config.js');
const data = JSON.parse(fs.readFileSync(DATA, 'utf8'));
const E = Object.values(data).filter(e => !e.declaredIn || e.declaredIn === e.owner);

// --- param-name extraction (same logic as the legacy generator) ---
function splitParams(s) {
    const o = []; let d = 0, c = '';
    for (const ch of String(s || '')) {
        if ('<(['.includes(ch)) d++; else if (')]>'.includes(ch)) d--;
        if (ch === ',' && d === 0) { o.push(c); c = ''; } else c += ch;
    }
    if (c.trim()) o.push(c);
    return o.map(x => x.trim()).filter(Boolean);
}
function simpleParams(params) {
    return splitParams(params).map(p => {
        p = p.replace(/=.*$/, '').trim();
        if (p === 'void' || p === '') return null;
        let m = p.match(/([A-Za-z_]\w*)\s*\[[^\]]*\]\s*$/) || p.match(/([A-Za-z_]\w*)\s*$/);
        return m ? m[1] : null;
    }).filter(Boolean).join(', ');
}
const ofStr = (e) => (e.of || []).join(' / ');
const tcCall = (e) => { const s = (e.signatures && e.signatures[0]) || { params: '' }; const p = simpleParams(s.params); return e.name + (p ? `(${p})` : '()'); };

// --- functions: group symbols-with-of by their display category ---
const fnGroups = {};
function fnGroup(disp) {
    return fnGroups[disp.id] || (fnGroups[disp.id] = { id: disp.id, name: disp.name, name_ja: disp.name_ja, name_ko: disp.name_ko || '', order: disp.order, mappings: [] });
}
for (const e of E.filter(e => e.of && e.of.length && e.kind === 'func' && e.category)) {
    const disp = categoryMapping[e.category];
    if (!disp) continue;
    const n = e.of_notes || {};
    fnGroup(disp).mappings.push({ of: ofStr(e), tc: tcCall(e), notes: n.en || '', notes_ja: n.ja || n.en || '', notes_ko: n.ko || '' });
}
// curated hand rows (Font, Video, Log, I/O, …) that aren't single api symbols
for (const entry of ofOnlyEntries) {
    const g = fnGroups[entry.category] || (fnGroups[entry.category] = { id: entry.category, name: entry.name, name_ja: entry.name_ja, name_ko: entry.name_ko || '', order: entry.order, mappings: [] });
    for (const m of entry.entries) g.mappings.push({ of: m.of, tc: m.tc, notes: m.notes || '', notes_ja: m.notes_ja || m.notes || '', notes_ko: m.notes_ko || '' });
}
const functions = Object.values(fnGroups).filter(g => g.mappings.length).sort((a, b) => a.order - b.order);
for (const g of functions) g.mappings.sort((a, b) => a.tc.localeCompare(b.tc));

// --- types: group types-with-of by their oF type category ---
const typeGroups = {};
for (const e of E.filter(e => e.of && e.of.length && e.kind === 'type' && e.of_category)) {
    const def = typeCategoryMapping[e.of_category];
    if (!def) continue;
    const g = typeGroups[def.id] || (typeGroups[def.id] = { id: def.id, name: def.name, name_ja: def.name_ja, name_ko: def.name_ko || '', order: def.order, mappings: [] });
    const d = e.description || {};
    g.mappings.push({ of: ofStr(e), tc: e.name, notes: d.en || '', notes_ja: d.ja || d.en || '', notes_ko: d.ko || '' });
}
const types = Object.values(typeGroups).sort((a, b) => a.order - b.order);
for (const g of types) g.mappings.sort((a, b) => a.tc.localeCompare(b.tc));

const json = JSON.stringify({ functions, types }, null, 2);

// --- markdown section (English only, function groups) ---
let md = '';
for (const g of functions) {
    md += `### **${g.name}**\n\n| openFrameworks | TrussC | Notes |\n|:---|:---|:---|\n`;
    for (const m of g.mappings) md += `| \`${m.of}\` | \`${m.tc}\` | ${m.notes} |\n`;
    md += '\n';
}

if (argv.includes('--dry')) { process.stdout.write(json + '\n\n===== MARKDOWN =====\n\n' + md); process.exit(0); }

fs.writeFileSync(OUT_JSON, json + '\n');
let doc = fs.readFileSync(OUT_MD, 'utf8');
const a = doc.indexOf(START), b = doc.indexOf(END);
if (a < 0 || b < 0) { console.error(`markers not found in ${OUT_MD}`); process.exit(1); }
doc = doc.slice(0, a + START.length) + '\n\n' + md + doc.slice(b);
fs.writeFileSync(OUT_MD, doc);

console.log('=== emit-of ===');
console.log(`function groups: ${functions.length}  type groups: ${types.length}`);
console.log(`rows: ${functions.reduce((n, g) => n + g.mappings.length, 0)} fn + ${types.reduce((n, g) => n + g.mappings.length, 0)} type`);
console.log(`wrote ${path.relative(process.cwd(), OUT_JSON)} + ${path.relative(process.cwd(), OUT_MD)} §5`);
