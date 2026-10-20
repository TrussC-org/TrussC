#!/usr/bin/env node
// ============================================================================
// emit-forai.js — emit the C++ API index into FOR_AI_ASSISTANT.md, and the
// details + code examples of each entry into FOR_AI_DETAILS.md, from the
// canonical reference-data.json (a downstream consumer; see README.md).
//
// Index policy (chosen with the user): DOCUMENTED symbols only, ONE LINE per
// symbol (overloads collapsed, "[+N]" marks extra overloads), English desc.
// Fields, operators and enum-members are excluded from the main listing
// (enum values are shown compactly in the Enums section instead). Undocumented
// symbols are intentionally omitted — an AI greps reference-data.json for them.
//
// Details policy: one `## <id>` section per entry that has details or a snippet
// (English only, like the MCP cards), in api-reference.toml order so related
// entries stay together. Hidden symbols and inherited copies are left out.
//
//   node emit-forai.js [--data <reference-data.json>] [--md <FOR_AI_ASSISTANT.md>]
//                      [--details <FOR_AI_DETAILS.md>] [--toml <api-reference.toml>]
//                      [--dry]        # print the index, then the details, don't write
// ============================================================================
const fs = require('fs');
const path = require('path');

const argv = process.argv.slice(2);
const argVal = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const DATA = argVal('--data', path.join(__dirname, 'reference-data.json'));
const MD = argVal('--md', path.join(__dirname, '../FOR_AI_ASSISTANT.md'));
const DETAILS = argVal('--details', path.join(__dirname, '../FOR_AI_DETAILS.md'));
const TOML_FILE = argVal('--toml', path.join(__dirname, 'api-reference.toml'));
const START = '<!-- API-INDEX-START -->';
const END = '<!-- API-INDEX-END -->';

const data = JSON.parse(fs.readFileSync(DATA, 'utf8'));
const cats = JSON.parse(fs.readFileSync(path.join(__dirname, 'categories.json'), 'utf8'));
const catName = Object.fromEntries(cats.map(c => [c.id, c.name]));
const catOrder = Object.fromEntries(cats.map(c => [c.id, c.order]));

const E = Object.values(data).filter(e => !e.hidden && (!e.declaredIn || e.declaredIn === e.owner));   // `hide = true` symbols are excluded from the public index
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
const qualifiedName = (ns, name) => (ns ? ns + '::' : '') + name;
const typeDecl = {};                           // owner -> its own type entry
for (const e of E) if (e.kind === 'type') typeDecl[qualifiedName(e.ns, e.name)] = e;
const membersByOwner = {};
for (const e of E) {
    if (!documented(e) || isOp(e)) continue;
    if (e.kind === 'method' && e.owner) {
        const owner = qualifiedName(e.ns, e.owner);
        (membersByOwner[owner] = membersByOwner[owner] || []).push(e);
    }
}
// a type appears if it has a documented decl OR documented members
const owners = new Set([...Object.keys(membersByOwner),
    ...Object.values(typeDecl).filter(documented).map(t => qualifiedName(t.ns, t.name))]);
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

// --- 5. FOR_AI_DETAILS.md: details + snippet per entry, for an LLM's reference file ---
// Only the TOML's key order is read here; entries it does not list (test
// fixtures) follow in id order.
const TOML = require(path.join(__dirname, '../node_modules/smol-toml'));
const tomlRank = new Map(Object.keys(TOML.parse(fs.readFileSync(TOML_FILE, 'utf8'))).map((id, i) => [id, i]));
const rank = e => tomlRank.has(e.id) ? tomlRank.get(e.id) : Infinity;
const detailed = E.filter(e => (e.details && e.details.en) || e.snippet)
    .sort((a, b) => rank(a) - rank(b) || a.id.localeCompare(b.id));
let details = `<!--
  ⚠️ This file is NOT human documentation.
  It is a reference file for an LLM assistant (Gemini Gem, custom GPT, Claude
  project, etc.), loaded next to FOR_AI_ASSISTANT.md: that file is the system
  prompt with the one-line API index, and this one holds the details and code
  examples of each entry. If you are a human looking for docs, start with
  docs/GET_STARTED.md or the web reference at https://trussc.org/reference/
  instead.
  Auto-generated by docs/reference/emit-forai.js from api-reference.toml.
  Do not edit by hand.
-->

# TrussC API details

Each section below is one entry of the API index in FOR_AI_ASSISTANT.md, headed \`## <id>\` with the same id (for example \`## Fbo::begin\`). It explains how the entry behaves and what to watch out for, often with a code example. Entries that have only a one-line description are in the index alone.
`;
for (const e of detailed) {
    details += `\n## ${e.id}\n`;
    if (e.details && e.details.en) details += `\n${e.details.en.trim()}\n`;
    if (e.snippet) details += `\n\`\`\`cpp\n${e.snippet.replace(/^\s*\n/, '').trimEnd()}\n\`\`\`\n`;
}

if (argv.includes('--dry')) { process.stdout.write(block + details); process.exit(0); }

let md = fs.readFileSync(MD, 'utf8');
const a = md.indexOf(START), b = md.indexOf(END);
if (a < 0 || b < 0) { console.error(`markers ${START} / ${END} not found in ${MD}`); process.exit(1); }
md = md.slice(0, a + START.length) + '\n' + block + md.slice(b);
fs.writeFileSync(MD, md);
fs.writeFileSync(DETAILS, details);

const kb = Math.round(block.length / 1024);
console.log('=== emit-forai ===');
console.log(`free fns: ${counts.fns}  types: ${counts.owners}  enums: ${counts.enums}`);
console.log(`index size: ${kb}kB  -> injected into ${path.relative(process.cwd(), MD)}`);
console.log(`details: ${detailed.length} entries, ${Math.round(details.length / 1024)}kB  -> ${path.relative(process.cwd(), DETAILS)}`);
