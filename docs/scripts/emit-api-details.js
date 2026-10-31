#!/usr/bin/env node
// ============================================================================
// emit-api-details.js — write the details + code examples of every API entry
// in api-reference.toml into one text file, as a reference file for an LLM
// assistant (Gemini Gem, custom GPT, Claude project, ...). FOR_AI_ASSISTANT.md
// stays the system prompt with the one-line index; this file is too large for
// a system prompt. Called by generateDocsForAI.sh; the output is not committed.
//
// One `## <id>` section per entry that has details or a snippet, English only,
// in TOML order. `hide = true` entries are left out.
//
//   node emit-api-details.js <out.txt> [--toml <api-reference.toml>]
// ============================================================================
const fs = require('fs');
const path = require('path');

const argv = process.argv.slice(2);
const argVal = (f, d) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : d; };
const OUT = argv[0];
const TOML_FILE = argVal('--toml', path.join(__dirname, '../reference/api-reference.toml'));
if (!OUT || OUT.startsWith('--')) { console.error('usage: emit-api-details.js <out.txt> [--toml <api-reference.toml>]'); process.exit(2); }

let TOML;
try { TOML = require(path.join(__dirname, '../node_modules/smol-toml')); }
catch { console.error('smol-toml not found: run `cd docs && npm ci` first'); process.exit(1); }

const prose = TOML.parse(fs.readFileSync(TOML_FILE, 'utf8'));
let out = `# TrussC API details

Each section below is one entry of the API index in FOR_AI_ASSISTANT.md, headed \`## <id>\` with the same id (for example \`## Fbo::begin\`). It explains how the entry behaves and what to watch out for, often with a code example. Entries that have only a one-line description are in the index alone.
`;
let n = 0;
for (const [id, p] of Object.entries(prose)) {
    const details = p.details && p.details.en;
    if (p.hide || !(details || p.snippet)) continue;
    out += `\n## ${id}\n`;
    if (details) out += `\n${details.trim()}\n`;
    if (p.snippet) out += `\n\`\`\`cpp\n${p.snippet.replace(/^\s*\n/, '').trimEnd()}\n\`\`\`\n`;
    n++;
}
fs.writeFileSync(OUT, out);
console.log(`api details: ${n} entries, ${Math.round(out.length / 1024)}kB -> ${OUT}`);
