// Generate the `colors:` section of api-definition.yaml from tcColor.h's
// `namespace colors` block. Textual insert (no js-yaml dump — preserve the
// curated file's formatting/comments).
const fs = require('fs');
const TR = '/Users/toru/Nextcloud/Make/TrussC/trussc';
const HEADER = TR + '/core/include/tcColor.h';
const YAML = TR + '/docs/api-definition.yaml';

// --- parse the namespace colors block ---
const lines = fs.readFileSync(HEADER, 'utf8').split('\n');
let inBlock = false, depth = 0;
const groups = [];
let cur = null;
for (const ln of lines) {
    if (!inBlock) { if (/namespace\s+colors\s*\{/.test(ln)) { inBlock = true; depth = 1; } continue; }
    // track braces to find the end of the namespace
    depth += (ln.match(/\{/g) || []).length - (ln.match(/\}/g) || []).length;
    if (depth <= 0) break;
    const gm = ln.match(/^\s*\/\/\s*(.+?)\s*$/);
    if (gm) { cur = { group: gm[1].replace(/\s*colors$/i, ''), items: [] }; groups.push(cur); continue; }
    const cm = ln.match(/^\s*inline\s+const\s+Color\s+(\w+)\s*\(([^)]*)\)\s*;/);
    if (cm) {
        const name = cm[1];
        const args = cm[2].split(',').map(s => parseFloat(s.trim()));
        const [r, g, b] = args;
        const a = args.length >= 4 ? args[3] : 1.0;
        if (!cur) cur = { group: 'Colors', items: [] }, groups.push(cur);
        cur.items.push({ name, rgba: [r, g, b, a] });
    }
}

const h2 = (v) => Math.round(v * 255).toString(16).toUpperCase().padStart(2, '0');
const hex = (r, g, b) => `#${h2(r)}${h2(g)}${h2(b)}`;
const num = (v) => (Number.isInteger(v) ? v.toFixed(1) : String(v));   // 1 -> "1.0"

// --- emit the yaml section ---
let out = [];
out.push('# ==========================================================================');
out.push('# Colors (named color constants in the trussc::colors namespace)');
out.push('# GENERATED from core/include/tcColor.h by scratchpad/gen-colors.js.');
out.push('# Grouped palette (see docs: rendered as swatches, not a flat constant list).');
out.push('# ==========================================================================');
out.push('colors:');
let total = 0;
for (const g of groups) {
    out.push(`  - group: "${g.group}"`);
    out.push('    items:');
    for (const c of g.items) {
        const [r, gg, b, a] = c.rgba;
        out.push(`      - { name: ${c.name}, hex: "${hex(r, gg, b)}", rgba: [${num(r)}, ${num(gg)}, ${num(b)}, ${num(a)}] }`);
        total++;
    }
}
out.push('');
const section = out.join('\n') + '\n';

// --- insert before the Keywords comment header (idempotent: strip an existing
//     generated colors section first, so re-running after a tcColor.h change
//     refreshes rather than duplicates) ---
let yaml = fs.readFileSync(YAML, 'utf8');
const colorsHdr = '# ==========================================================================\n# Colors (named color';
const kwHdr = '# ==========================================================================\n# Keywords';
const cIdx = yaml.indexOf(colorsHdr);
if (cIdx >= 0) {
    const kIdx = yaml.indexOf(kwHdr, cIdx);
    yaml = yaml.slice(0, cIdx) + yaml.slice(kIdx);   // remove old colors section
}
const idx = yaml.indexOf(kwHdr);
if (idx < 0) { console.error('keywords marker not found'); process.exit(1); }
const next = yaml.slice(0, idx) + section + '\n' + yaml.slice(idx);
fs.writeFileSync(YAML, next);
console.log(`inserted colors: ${groups.length} groups, ${total} colors`);
console.log('groups:', groups.map(g => `${g.group}(${g.items.length})`).join(', '));
