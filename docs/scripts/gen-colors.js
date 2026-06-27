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
const r3 = (v) => Math.round(v * 1000) / 1000;        // keep rgba tidy

// --- emit reference/colors.json (the grouped swatch palette) ---
const OUT = TR + '/docs/reference/colors.json';
const palette = groups.map(g => ({
    group: g.group,
    items: g.items.map(c => ({ name: c.name, hex: hex(c.rgba[0], c.rgba[1], c.rgba[2]), rgba: c.rgba.map(r3) })),
}));
fs.writeFileSync(OUT, JSON.stringify(palette, null, 2) + '\n');
const total = palette.reduce((n, g) => n + g.items.length, 0);
console.log(`wrote ${OUT}: ${palette.length} groups, ${total} colors`);
console.log('groups:', palette.map(g => `${g.group}(${g.items.length})`).join(', '));
