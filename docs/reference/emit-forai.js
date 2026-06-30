#!/usr/bin/env node
// ============================================================================
// emit-forai.js — emit the C++ API index into FOR_AI_ASSISTANT.md from the
// canonical reference-data.json (a downstream consumer; see README.md).
//
// Index policy (chosen with the user): DOCUMENTED symbols only, ONE LINE per
// symbol (overloads collapsed, "[+N]" marks extra overloads), English desc.
// Fields, operators and enum-members are excluded from the main listing
// (enum values are shown compactly in the Enums section instead). Undocumented
// symbols are intentionally omitted — an AI greps reference-data.json for them.
//
//   node emit-forai.js [--data <reference-data.json>] [--md <FOR_AI_ASSISTANT.md>]
//                      [--dry]        # print the index to stdout, don't write
// ============================================================================
const fs = require('fs');
const path = require('path');

const argv = process.argv.slice(2);
const argVal = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const DATA = argVal('--data', path.join(__dirname, 'reference-data.json'));
const MD = argVal('--md', path.join(__dirname, '../FOR_AI_ASSISTANT.md'));
const START = '<!-- API-INDEX-START -->';
const END = '<!-- API-INDEX-END -->';

const data = JSON.parse(fs.readFileSync(DATA, 'utf8'));
const cats = JSON.parse(fs.readFileSync(path.join(__dirname, 'categories.json'), 'utf8'));
const catName = Object.fromEntries(cats.map(c => [c.id, c.name]));
const catOrder = Object.fromEntries(cats.map(c => [c.id, c.order]));

const E = Object.values(data).filter(e => !e.hidden);   // `hide = true` symbols are excluded from the public index
const isOp = e => /::operator/.test(e.id);
const documented = e => e.documented;
// DISPLAY-only: drop a single trailing "_" from param names (member-shadow
// convention, e.g. Vec2(float x_, float y_)). reference-data keeps the real name.
const stripU = (s) => String(s || '').replace(/_(?=\s*(?:,|=|$))/g, '');

// one collapsed line for a callable/type symbol
function line(e) {
    const s = (e.signatures && e.signatures[0]) || null;
    const extra = e.signatures && e.signatures.length > 1 ? ` [+${e.signatures.length - 1}]` : '';
    const dep = e.deprecated ? ' ⚠️deprecated' : '';
    const prov = e.provider === 'std' ? ' [std]' : '';
    const plat = e.platforms ? ` [${e.platforms.join(',')}]` : '';
    const desc = (e.description && e.description.en) ? '  // ' + e.description.en.replace(/\s*\n\s*/g, ' ') : '';
    let sig;
    if (s && (e.kind === 'method' || e.kind === 'func')) {
        sig = `${s.ret ? s.ret + ' ' : ''}${e.id}(${stripU(s.params)})${s.const ? ' const' : ''}`;
    } else if (e.kind === 'type') {
        sig = `${e.tparams ? 'template<' + e.tparams.join(', ') + '> ' : ''}class ${e.id}`;
    } else if (e.kind === 'typedef') {
        sig = `using ${e.id}`;
    } else {                                   // var / const-like
        sig = (s && s.ret ? s.ret + ' ' : '') + e.id;
    }
    return `${sig}${extra}${dep}${prov}${plat}${desc}`;
}

let out = '';

// --- 1. free functions & namespaced free fns & loose consts, grouped by category ---
const freeFns = E.filter(e => documented(e) && (e.kind === 'func' || (e.kind === 'var' && !e.ns)) && !isOp(e));
const byCat = {};
for (const e of freeFns) { const c = e.category || '__other'; (byCat[c] = byCat[c] || []).push(e); }
const catIds = Object.keys(byCat).sort((a, b) =>
    (a === '__other') - (b === '__other') || (catOrder[a] ?? 1e9) - (catOrder[b] ?? 1e9));
for (const cid of catIds) {
    out += `\n### ${cid === '__other' ? 'Other' : (catName[cid] || cid)}\n\n\`\`\`cpp\n`;
    for (const e of byCat[cid].sort((a, b) => a.id.localeCompare(b.id))) out += line(e) + '\n';
    out += '```\n';
}

// --- 2. type members, grouped per owner type (methods + static methods) ---
const typeDecl = {};                           // owner -> its own type entry
for (const e of E) if (e.kind === 'type') typeDecl[e.name] = e;
const membersByOwner = {};
for (const e of E) {
    if (!documented(e) || isOp(e)) continue;
    if (e.kind === 'method' && e.owner) (membersByOwner[e.owner] = membersByOwner[e.owner] || []).push(e);
}
// a type appears if it has a documented decl OR documented members
const owners = new Set([...Object.keys(membersByOwner),
    ...Object.values(typeDecl).filter(documented).map(t => t.name)]);
out += '\n## Types\n';
for (const owner of [...owners].sort()) {
    const decl = typeDecl[owner];
    const desc = decl && decl.description && decl.description.en ? ' — ' + decl.description.en.replace(/\s*\n\s*/g, ' ') : '';
    const tp = decl && decl.tparams ? `<${decl.tparams.join(', ')}>` : '';
    out += `\n### ${owner}${tp}${desc}\n\n\`\`\`cpp\n`;
    for (const e of (membersByOwner[owner] || []).sort((a, b) => a.id.localeCompare(b.id))) out += line(e) + '\n';
    out += '```\n';
}

// --- 3. enums with their values (members captured by structure.js), compact ---
const enums = E.filter(e => e.kind === 'enum' && e.members && e.members.length).sort((a, b) => a.id.localeCompare(b.id));
if (enums.length) {
    out += '\n## Enums\n\n```cpp\n';
    for (const e of enums) {
        const desc = e.description && e.description.en ? '  // ' + e.description.en.replace(/\s*\n\s*/g, ' ') : '';
        out += `enum ${e.id} { ${e.members.map(m => m.name).join(', ')} }${desc}\n`;
    }
    out += '```\n';
}

// --- 4. type aliases ---
const typedefs = E.filter(e => e.kind === 'typedef' && documented(e)).sort((a, b) => a.id.localeCompare(b.id));
if (typedefs.length) {
    out += '\n## Type aliases\n\n```cpp\n';
    for (const e of typedefs) out += line(e) + '\n';
    out += '```\n';
}

const counts = { fns: freeFns.length, owners: owners.size, enums: enums.length };
const header = `\n_Auto-generated C++ API index from \`reference-data.json\` (structure from the C++ AST, prose from \`api-reference.toml\`). Documented symbols only — undocumented APIs are findable via the interactive reference: https://trussc.org/reference/. Overloads collapsed; \`[+N]\` = N more overloads; \`[std]\` = provided by std:: (available via \`using namespace std\`); \`[macos,ios,…]\` = available only on those platforms (no marker = all platforms)._\n`;
const block = header + out + '\n';

if (argv.includes('--dry')) { process.stdout.write(block); process.exit(0); }

let md = fs.readFileSync(MD, 'utf8');
const a = md.indexOf(START), b = md.indexOf(END);
if (a < 0 || b < 0) { console.error(`markers ${START} / ${END} not found in ${MD}`); process.exit(1); }
md = md.slice(0, a + START.length) + '\n' + block + md.slice(b);
fs.writeFileSync(MD, md);

const kb = Math.round(block.length / 1024);
console.log('=== emit-forai ===');
console.log(`free fns: ${counts.fns}  types: ${counts.owners}  enums: ${counts.enums}`);
console.log(`index size: ${kb}kB  -> injected into ${path.relative(process.cwd(), MD)}`);
