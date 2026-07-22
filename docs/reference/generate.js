#!/usr/bin/env node
// ============================================================================
// generate.js — JOIN the AST structure with the TOML prose into the reference
// outputs (B4). Replaces generate-docs.js: structure comes from structure.js
// (the C++ AST), prose from api-reference.toml, joined by symbol-id.
//
//   node generate.js [--ast <cached>] [--structure <json>]
//                    [--out-json <f>] [--out-md <f>]
//
// Default outputs: docs/reference/reference-data.json (canonical joined data,
// downstream-consumable) and docs/reference/API_INDEX.md (human/AI index).
// ============================================================================
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const TOML = require(path.join(__dirname, '../node_modules/smol-toml'));

const argv = process.argv.slice(2);
const argVal = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const OUT_JSON = argVal('--out-json', path.join(__dirname, 'reference-data.json'));
const OUT_MD = argVal('--out-md', path.join(__dirname, 'API_INDEX.md'));

// --- structure (AST) ---
let structure;
const sf = argVal('--structure');
if (sf) structure = JSON.parse(fs.readFileSync(sf, 'utf8'));
else {
    const a = ['--max-old-space-size=8192', path.join(__dirname, 'structure.js')];
    if (argVal('--ast')) a.push('--ast', argVal('--ast'));
    const tmp = path.join(require('os').tmpdir(), 'tc-ref-structure.json');
    a.push('--json', tmp); process.stderr.write('generate: deriving structure…\n');
    execFileSync('node', a, { maxBuffer: 256 * 1024 * 1024 });
    structure = JSON.parse(fs.readFileSync(tmp, 'utf8'));
}
// --- refuse a partial AST: this file RENDERS TYPES ---------------------------
// structure.js marks __meta.partialAst when clang failed mid-parse (typical
// cause: the gitignored generated shader headers, core/include/tc/gpu/shaders/
// *.glsl.h, are missing in a fresh worktree). Names survive a partial AST but
// unresolved template types degrade to `int`, silently corrupting every
// rendered signature here and downstream (emit-forai, luagen, web emits).
if (structure && structure.__meta) {
    const partial = structure.__meta.partialAst;
    delete structure.__meta;
    if (partial && process.env.TC_ALLOW_PARTIAL_AST !== '1') {
        console.error(
            'generate: REFUSING to render from a PARTIAL AST (clang failed mid-parse; see diagnostics above).\n' +
            'Typical cause: generated shader headers missing in a fresh worktree.\n' +
            'Fix: run a build once (cmake configure+build generates core/include/tc/gpu/shaders/*.glsl.h),\n' +
            '     or copy that directory from a built checkout.\n' +
            'Override (types WILL be wrong): TC_ALLOW_PARTIAL_AST=1');
        process.exit(1);
    }
}

// --- prose (TOML) ---
const prose = TOML.parse(fs.readFileSync(path.join(__dirname, 'api-reference.toml'), 'utf8'));

// --- join: structure is the spine; prose enriches ---
const joined = {};
for (const id in structure) {
    const s = structure[id], p = prose[id] || {};
    joined[id] = {
        id, kind: s.kind, owner: s.owner, name: s.name, ns: s.ns,
        signatures: s.signatures, static: s.static || undefined, tparams: s.tparams,
        type: s.type,                                      // field/property C++ type (e.g. "float")
        access: s.access,                                  // 'protected' (public omitted); web emitter hides protected
        hidden: p.hide ? true : undefined,                 // `hide = true` in the toml: public C++ but not API — kept in the data (CI/luagen still see it), excluded from the docs
        members: s.members,                                // enum values (enums only)
        constructors: s.constructors,                      // public ctor signatures (types only)
        provider: s.provider,                              // 'std' for curated std:: symbols
        platforms: s.platforms, lua_bind: s.lua_bind,      // from TC_PLATFORMS / TC_LUA_BIND

        deprecated: s.deprecated,                          // from C++ [[deprecated]]
        category: p.category, keywords: p.keywords, of: p.of,
        of_category: p.of_category, of_notes: p.of_notes,   // oF mapping prose
        description: p.description,                          // {en,ja,ko}
        details: p.details, snippet: p.snippet,             // snippet = authorable example code
        related: p.related, platform_note: p.platform_note, // related symbols, platform behaviour note
        value_desc: p.value_desc,                           // enum: per-value descriptions {Member:{en,ja,ko}}
        documented: !!(p.description && p.description.en),
    };
}
fs.writeFileSync(OUT_JSON, JSON.stringify(joined, null, 0));

// --- human/AI markdown index, grouped by category (prose) then owner ---
const sigStr = (e) => {
    const r = (e.signatures && e.signatures[0]) || { ret: '', params: '' };
    const disp = e.kind === 'method' || e.kind === 'func'
        ? `${r.ret ? r.ret + ' ' : ''}${e.id}(${r.params})${r.const ? ' const' : ''}`
        : e.id + (e.kind === 'type' ? '' : '');
    return disp;
};
const groups = {};
for (const id in joined) {
    const e = joined[id];
    const g = e.category || (e.owner ? `type: ${e.owner}` : `(${e.kind})`);
    (groups[g] = groups[g] || []).push(e);
}
let md = `# TrussC API Index\n\n> Generated by docs/reference/generate.js — structure from the C++ AST, prose from api-reference.toml.\n`;
md += `> ${Object.keys(joined).length} symbols, ${Object.values(joined).filter(e => e.documented).length} documented.\n`;
for (const g of Object.keys(groups).sort()) {
    md += `\n## ${g}\n\n`;
    for (const e of groups[g].sort((a, b) => a.id.localeCompare(b.id))) {
        const d = (e.description && e.description.en) ? ' — ' + e.description.en : '';
        const dep = e.deprecated ? ' ⚠️deprecated' : '';
        md += `- \`${sigStr(e)}\`${dep}${d}\n`;
        for (let i = 1; i < (e.signatures || []).length; i++) md += `  - \`(${e.signatures[i].params})\`\n`;
    }
}
fs.writeFileSync(OUT_MD, md);

const visible = Object.values(joined).filter(e => !e.hidden);       // hidden = public-but-not-API, excluded from the doc surface
const documented = visible.filter(e => e.documented).length;
const hiddenCount = Object.keys(joined).length - visible.length;
console.log('=== generate (join structure + prose) ===');
console.log(`symbols joined : ${Object.keys(joined).length} (${hiddenCount} hidden)`);
console.log(`documented     : ${documented} / ${visible.length}  (${(documented / visible.length * 100).toFixed(0)}%)`);
console.log(`wrote ${path.relative(process.cwd(), OUT_JSON)} + ${path.relative(process.cwd(), OUT_MD)}`);
