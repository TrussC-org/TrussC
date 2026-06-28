#!/usr/bin/env node
/**
 * emit-web.js — emit the website's web-player API data (trussc.org/generated/
 * trussc-api.js). SUPERSEDES the trussc-api.js path of scripts/generate-docs.js.
 *
 * APPROACH (a): keep the intermediate web artifact in its existing shape (so
 * reference/reference.js and examples/code-reference.js keep working unchanged),
 * but source its symbol PROSE + STRUCTURE from the overhauled canonical data,
 * docs/reference/reference-data.json, instead of re-reading the deprecated
 * api-definition.yaml for that.
 *
 *   reference-data.json (CANONICAL) supplies, per symbol-id:
 *       description {en,ja,ko}, keywords, details, deprecated.reason (from the
 *       C++ [[deprecated]] attribute), and the clean `Owner::name` ids that make
 *       static methods render as `Color::fromOKLab` (never `Color_fromOKLab`).
 *
 *   api-definition.yaml is still joined as an AUXILIARY sidecar for everything
 *   reference-data.json does NOT (yet) carry — see README §"Notes for consumers":
 *       the category/type DISPLAY STRUCTURE + order, signatures-as-authored,
 *       related[], platforms/platformNote, operator schema, property types,
 *       constructors, enum value numbers + per-value descriptions, constant
 *       values, macros, top-level language keywords, deprecated.replacement/url,
 *       and the grouped color palette.
 *   (Once those migrate into the reference pipeline the yaml join can be dropped
 *    and api-definition.yaml deleted — that is the remaining retirement step.)
 *
 *   examples.json + scan-examples.js → symbol → example links (unchanged).
 *
 * Usage:  node emit-web.js [--dry]
 * Output: ../../../trussc.org/generated/trussc-api.js
 */

const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');
const { execSync } = require('child_process');
const { buildReverseIndex } = require('./scan-examples.js');

// --- paths -------------------------------------------------------------------
const REF_DATA = path.join(__dirname, '../reference/reference-data.json');
const CATEGORIES = path.join(__dirname, '../reference/categories.json');
const API_YAML = path.join(__dirname, '../api-definition.yaml');                 // AUX
const TRUSSC_API_JS = path.join(__dirname, '../../../trussc.org/generated/trussc-api.js');
const EXAMPLES_JSON = path.join(__dirname, '../../../trussc.org/examples/examples.json');

const args = process.argv.slice(2);
const DRY = args.includes('--dry');

// --- param-name extraction (same logic the legacy generator used) ------------
function splitParams(s) {
    const o = []; let d = 0, c = '';
    for (const ch of String(s || '')) {
        if ('<(['.includes(ch)) d++; else if (')]>'.includes(ch)) d--;
        if (ch === ',' && d === 0) { o.push(c); c = ''; } else c += ch;
    }
    if (c.trim()) o.push(c);
    return o.map(x => x.trim()).filter(Boolean);
}
function simpleParams(params) {                       // "float x, const Vec2& v" -> "x, v"
    return splitParams(params).map(p => {
        p = p.replace(/=.*$/, '').trim();
        if (p === 'void' || p === '') return null;
        let m = p.match(/([A-Za-z_]\w*)\s*\[[^\]]*\]\s*$/);   // Type name[N]
        if (m) return m[1];
        m = p.match(/([A-Za-z_]\w*)\s*$/);
        return m ? m[1] : null;
    }).filter(Boolean).join(', ');
}
// DISPLAY-only: drop a single trailing "_" from param names (member-shadow
// convention, e.g. Vec2(float x_, float y_)). reference-data keeps the real name.
const stripU = (s) => String(s || '').replace(/_(?=\s*(?:,|=|$))/g, '');

function getVersion() {
    try {
        return execSync('git describe --tags --abbrev=0', { cwd: path.join(__dirname, '../..') })
            .toString().trim();
    } catch { return 'unknown'; }
}

// === canonical: reference-data.json (prose + structure of truth) ============
const REF = JSON.parse(fs.readFileSync(REF_DATA, 'utf8'));
const COLORS = JSON.parse(fs.readFileSync(path.join(__dirname, '../reference/colors.json'), 'utf8'));
let refHits = 0, refMiss = 0;

// Combined (legacy) en/ja/ko description trio: reference-data wins when it
// documents the symbol; otherwise fall back to the yaml-authored prose.
const _t = (s) => String(s == null ? '' : s).trim();   // reference-data prose carries trailing \n
// details may be a {en,ja,ko} object (long-form prose) or a legacy en-only string
const detailsOf = (det) => {
    if (!det) return {};
    if (typeof det === 'object') {
        const o = {}; if (_t(det.en)) o.details = _t(det.en);
        if (_t(det.ja)) o.details_ja = _t(det.ja);
        if (_t(det.ko)) o.details_ko = _t(det.ko);
        return o;
    }
    return { details: _t(det) };
};
// Prose precedence: reference-data.json (the api-reference.toml prose, keyed by
// symbol-id) is now the source of truth; the legacy yaml is only a transitional
// fallback for anything not yet in reference-data. `allowRef` lets a caller veto
// the REF lookup when an unsafe bare-name join would otherwise apply.
function descTrio(refId, ym, allowRef = true) {
    const r = allowRef && REF[refId];
    if (r) refHits++; else refMiss++;
    const d = (r && r.description) || {};
    if (d.en) return { desc: _t(d.en), desc_ja: _t(d.ja), desc_ko: _t(d.ko) };
    if (ym && _t(ym.description)) {                     // transitional yaml fallback
        return { desc: _t(ym.description), desc_ja: _t(ym.description_ja), desc_ko: _t(ym.description_ko) };
    }
    return { desc: '', desc_ja: '', desc_ko: '' };
}
const refKeywords = (refId, ym, allowRef = true) => {
    const r = allowRef && REF[refId];
    if (r && Array.isArray(r.keywords) && r.keywords.length) return r.keywords;
    return (ym && ym.keywords) || [];                  // transitional yaml fallback
};
// Deprecation: reason from the C++ source (reference-data) wins; the human-facing
// replacement/url come from the yaml sidecar.
function mergeDeprecated(refId, ym, trustRef = true) {
    const r = trustRef && REF[refId] && REF[refId].deprecated;
    const y = ym && ym.deprecated;
    if (!r && !y) return undefined;
    const reason = (r && r.reason) || (y && y.reason) || '';
    const out = { reason };
    let replacement = y && y.replacement;
    if (!replacement && reason) {                          // parse "Use/call/Renamed to `X()`" out of the reason -> clickable link
        const m = reason.match(/(?:Use|call|Renamed to)\s+`?([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)`?\s*\(/);
        if (m) replacement = m[1];
    }
    if (replacement) out.replacement = replacement;
    if (y && y.url) out.url = y.url;
    return out;
}

// === auxiliary: api-definition.yaml (display structure + un-migrated fields) =
// Legacy yaml sidecar — OPTIONAL now (being retired). All prose comes from
// reference-data; macros/keywords/constant-values/example-links come from
// reference/extras.json; colors from reference/colors.json.
const API = fs.existsSync(API_YAML) ? yaml.load(fs.readFileSync(API_YAML, 'utf8')) : { categories: [], types: [], enums: [], constants: [] };
const EXTRAS = JSON.parse(fs.readFileSync(path.join(__dirname, '../reference/extras.json'), 'utf8'));

// --- yaml sidecar lookups, keyed by the SAME symbol-id reference-data uses ---
// The full symbol surface is now driven by reference-data.json (every public
// symbol). The yaml is consulted only as an id-keyed sidecar for the fields it
// still carries that reference-data does not yet: authored sigDesc, related[],
// platforms, details (ja/ko), constructors, property types, operator schema.
const YML_FUNC = new Map();   // free-fn id -> yaml function entry
const YML_TYPE = new Map();   // type name  -> yaml type entry
const YML_METHOD = new Map(); // `Type::method` id -> yaml method/static_method entry
const YML_PROP = new Map();   // `Type::field` id  -> yaml property entry
const YML_ENUM = new Map();   // enum name  -> yaml enum entry
const YML_CONST = new Map();  // const name -> yaml constant entry
for (const c of API.categories || [])
    for (const f of c.functions || []) if (!YML_FUNC.has(f.name)) YML_FUNC.set(f.name, f);
for (const t of API.types || []) {
    YML_TYPE.set(t.name, t);
    for (const m of (t.methods || [])) YML_METHOD.set(`${t.name}::${m.name}`, m);
    for (const m of (t.static_methods || [])) YML_METHOD.set(`${t.name}::${m.name}`, m);
    for (const p of (t.properties || [])) YML_PROP.set(`${t.name}::${p.name}`, p);
}
for (const e of API.enums || []) YML_ENUM.set(e.name, e);
for (const c of API.constants || []) YML_CONST.set(c.name, c);

// CATEGORIES.json provides the category DISPLAY name + ja/ko + order, keyed by
// the same category id reference-data symbols carry. This is the grouping/order
// source of truth for free functions.
const CATS = JSON.parse(fs.readFileSync(CATEGORIES, 'utf8'));
const CAT_BY_ID = new Map(CATS.map(c => [c.id, c]));

// --- operator rendering (schema lives in the yaml only) ---------------------
function opIsConst(sym) { return !(/=$/.test(sym) && !/^(==|!=|<=|>=)$/.test(sym)) && sym !== '[]'; }
const _bare = (t) => String(t || '').replace(/^const\s+/, '').replace(/\s*&$/, '').trim();
function opDisplay(op, owner) {
    const s = op.symbol, res = op.result;
    if (op.lhs !== undefined) return `${_bare(op.lhs)} ${s} ${_bare(op.rhs)} → ${res}`;
    if (op.unary) return `${s}${owner} → ${res}`;
    if (s === '[]') return `${owner}[${_bare(op.rhs)}] → ${res}`;
    return `${owner} ${s} ${_bare(op.rhs)} → ${res}`;
}
function opCpp(op, owner) {
    const res = op.result || owner;
    if (op.lhs !== undefined) return `${res} operator${op.symbol}(${op.lhs}, ${op.rhs})`;
    if (op.unary) return `${res} operator${op.symbol}()${opIsConst(op.symbol) ? ' const' : ''}`;
    return `${res} operator${op.symbol}(${op.rhs || ''})${opIsConst(op.symbol) ? ' const' : ''}`;
}
// Operator schema for a type/enum, DERIVED from the AST operator methods/funcs in
// reference-data (member operators owned by `owner`, plus free operators where
// `owner` is one of the two operands). Per-operator prose still comes from the
// yaml sidecar (matched by symbol) until prose moves to the toml.
function mapOperators(owner, ym) {
    const ops = [];
    for (const e of Object.values(REF)) {
        const sig = e.signatures && e.signatures[0];
        if (!sig) continue;
        const isMember = e.kind === 'method' && e.owner === owner && /^operator/.test(e.name || '');
        const isFree = e.kind === 'func' && /^operator/.test(e.name || '');
        if (!isMember && !isFree) continue;
        const symbol = (e.name || '').slice(8);                  // after "operator"
        if (!symbol || /^[a-zA-Z\s(]/.test(symbol)) continue;    // skip conversion / call operators
        const args = sig.args || [];
        if (isMember) {
            const op = { symbol, result: sig.ret };
            if (args.length === 0) op.unary = true; else op.rhs = args[0].type;
            ops.push(op);
        } else {
            if (args.length !== 2 || !args.some(a => _bare(a.type) === owner)) continue;
            ops.push({ symbol, lhs: args[0].type, rhs: args[1].type, result: sig.ret, free: true });
        }
    }
    return ops.map(o => {
        const opId = o.free ? `operator${o.symbol}` : `${owner}::operator${o.symbol}`;   // operator prose lives on its own symbol entry
        const rd = (REF[opId] && REF[opId].description) || {};
        return {
            symbol: o.symbol,
            signature: opDisplay(o, owner),
            cpp: opCpp(o, owner),
            free: !!o.free,
            desc: rd.en || '',
            desc_ja: rd.ja || '',
            desc_ko: rd.ko || '',
        };
    });
}

// Optional platform-support annotation: the platform LIST comes from the C++
// source (TC_PLATFORMS, carried in reference-data.json as `ref.platforms`); any
// authored note still comes from the yaml sidecar.
function attachPlatforms(entry, ref, ym) {
    // C++ TC_PLATFORMS (reference-data) wins; fall back to any legacy yaml platforms.
    const plats = (ref && Array.isArray(ref.platforms) && ref.platforms.length) ? ref.platforms
        : (ym && Array.isArray(ym.platforms) && ym.platforms.length ? ym.platforms : null);
    if (plats) entry.platforms = plats;
    const pn = ref && ref.platform_note;
    if (pn && pn.en) {
        entry.platformNote = pn.en;
        if (pn.ja) entry.platformNote_ja = pn.ja;
        if (pn.ko) entry.platformNote_ko = pn.ko;
    }
    return entry;
}

// === auxiliary: example links (examples.json + scan-examples reverse index) ==
const EXAMPLE_CAP = 3;
function buildExamplesMap() {
    let exLookup = new Map();
    try {
        const exJson = JSON.parse(fs.readFileSync(EXAMPLES_JSON, 'utf8'));
        for (const [group, cat] of Object.entries(exJson.examples || {}))
            for (const it of (cat.items || [])) exLookup.set(it.name, { group, web: it.webSupported !== false });
    } catch (e) {
        console.log(`  Warning: examples.json unreadable (${e.message}); skipping example links`);
        return {};
    }
    const funcs = new Set(), types = new Set(), excluded = new Set();
    for (const e of Object.values(REF)) {
        if (e.kind === 'func') {
            if (e.category === 'lifecycle' || e.category === 'events') excluded.add(e.name);
            else funcs.add(e.name);
        } else if (e.kind === 'type') types.add(e.name);
    }
    for (const n of excluded) funcs.delete(n);

    let rev = { index: {} };
    try { rev = buildReverseIndex({ funcs, types, excluded }); }
    catch (e) { console.log(`  Warning: example scan failed (${e.message}); manual links only`); }

    const resolve = (symName, manual) => {
        const names = (Array.isArray(manual) && manual.length) ? manual : (rev.index[symName] || []).map(e => e.name);
        const out = [];
        for (const n of names.slice(0, EXAMPLE_CAP)) {
            const info = exLookup.get(n);
            if (info && info.web) out.push({ name: n, group: info.group });
        }
        return out;
    };
    const manual = EXTRAS.examples_manual || {};
    const map = {};
    for (const name of funcs) {
        const ex = resolve(name, manual[name]);
        if (ex.length) map[name] = ex;
    }
    for (const name of types) {
        const ex = resolve(name, manual['type:' + name]);
        if (ex.length) map['type:' + name] = ex;
    }
    return map;
}

// === build the combined (all-languages) artifact ============================
//
// FULL SURFACE: reference-data.json is now the iteration driver — every public
// symbol it carries is emitted, documented or not (undocumented → empty desc
// strings). The api-definition.yaml is consulted ONLY as an id-keyed sidecar
// (YML_* maps) for fields reference-data does not yet carry: authored sigDesc,
// related[], details ja/ko, constructors, property types, operators, plus the
// per-enum-value numbers/descriptions, constant values, macros, top-level
// language keywords and the color palette. The output JSON shape is unchanged.
function build(examplesMap) {
    const version = getVersion();
    const REF_VALS = Object.values(REF);

    // operator member/free fns are rendered via the yaml operator schema (under
    // their owning type/enum), never as plain function/method entries.
    const isOperator = (sym) => sym.name.startsWith('operator');

    // --- categories (free functions, grouped by reference-data `category`) ---
    // One emitted entry PER overload signature (legacy-flattened shape).
    const OTHER_ID = '__other__';
    const funcsByCat = new Map();   // catId -> [func symbol, …]
    for (const sym of REF_VALS) {
        if (sym.kind !== 'func' || isOperator(sym)) continue;
        const catId = sym.category && CAT_BY_ID.has(sym.category) ? sym.category : OTHER_ID;
        if (!funcsByCat.has(catId)) funcsByCat.set(catId, []);
        funcsByCat.get(catId).push(sym);
    }
    // Emit categories in categories.json order, then any leftover ids, then Other.
    const orderedCatIds = [
        ...CATS.slice().sort((a, b) => (a.order ?? 1e9) - (b.order ?? 1e9)).map(c => c.id)
            .filter(id => funcsByCat.has(id)),
        ...[...funcsByCat.keys()].filter(id => id !== OTHER_ID && !CAT_BY_ID.has(id)),
        ...(funcsByCat.has(OTHER_ID) ? [OTHER_ID] : []),
    ];
    const catDisplay = (id) => {
        if (id === OTHER_ID) return { name: 'Other', name_ja: 'その他', name_ko: '기타' };
        const c = CAT_BY_ID.get(id);
        return c ? { name: c.name, name_ja: c.name_ja || '', name_ko: c.name_ko || '' }
                 : { name: id, name_ja: '', name_ko: '' };
    };

    const categories = [];
    for (const catId of orderedCatIds) {
        const functions = [];
        for (const sym of funcsByCat.get(catId)) {
            const refId = sym.id;
            const ym = YML_FUNC.get(refId);                  // sidecar (may be undefined)
            const prose = descTrio(refId, ym);
            const keywords = refKeywords(refId, ym);
            const returnType = (sym.signatures[0] && sym.signatures[0].ret) ??
                (ym && ym.return !== undefined ? ym.return : null);
            const sigs = sym.signatures.length ? sym.signatures : [{ params: '' }];
            for (let i = 0; i < sigs.length; i++) {
                const sig = sigs[i];
                const { desc, desc_ja, desc_ko } = prose;
                const entry = {
                    name: sym.name,
                    params: stripU(simpleParams(sig.params)),
                    params_typed: stripU(sig.params || ''),
                    return_type: (sig.ret !== undefined ? sig.ret : returnType) ?? null,
                    desc,
                    keywords,
                    desc_ja,
                    desc_ko,
                };
                if (sym.provider === 'std') entry.std = true;   // provided by std:: (available via `using namespace std`), no TrussC wrapper
                // authored per-signature prose, matched positionally against yaml
                const ysig = ym && ym.signatures && ym.signatures[i];
                if (ysig && ysig.description) {
                    entry.sigDesc = ysig.description;
                    if (ysig.description_ja) entry.sigDesc_ja = ysig.description_ja;
                    if (ysig.description_ko) entry.sigDesc_ko = ysig.description_ko;
                }
                if (Array.isArray(sym.related) && sym.related.length) entry.related = sym.related;
                const dep = mergeDeprecated(refId, ym);
                if (dep) entry.deprecated = dep;
                // details: prefer the curated yaml sidecar (keeps ja/ko); fall back
                // to reference-data's en-only string (trimmed).
                const refDetails = REF[refId] && REF[refId].details;
                if (ym && ym.details) {
                    entry.details = ym.details;
                    if (ym.details_ja) entry.details_ja = ym.details_ja;
                    if (ym.details_ko) entry.details_ko = ym.details_ko;
                } else if (refDetails) {
                    Object.assign(entry, detailsOf(refDetails));
                }
                if (examplesMap[sym.name]) entry.examples = examplesMap[sym.name];
                attachPlatforms(entry, sym, ym);
                functions.push(entry);
            }
        }
        if (functions.length === 0) continue;
        // key order matches the legacy generator (name, functions, then localized names)
        const d = catDisplay(catId);
        categories.push({ name: d.name, functions, name_ja: d.name_ja, name_ko: d.name_ko });
    }

    // --- constants (kind=="var", not owned, not an enum member) ---
    // Enum members live in their enum's `members` array (emitted under enums),
    // never as standalone vars in reference-data, so every non-owned var here is
    // a genuine constant. Values come from the yaml sidecar when present.
    const ENUM_NS = new Set(REF_VALS.filter(v => v.kind === 'enum').map(v => v.name));
    const constants = [];
    for (const sym of REF_VALS) {
        if (sym.kind !== 'var' || sym.owner) continue;
        if (sym.ns && ENUM_NS.has(sym.ns)) continue;         // defensive: skip enum members
        if (sym.ns === 'colors') continue;                   // named colors are emitted in the `colors` palette, not as flat constants
        const { desc, desc_ja, desc_ko } = descTrio(sym.id, null);
        const ref = REF[sym.id];
        const c = {
            name: sym.id,
            value: EXTRAS.constants[sym.id] ?? EXTRAS.constants[sym.name],   // curated value (KEY_* etc. aren't in the AST)
            desc, desc_ja, desc_ko,
            keywords: refKeywords(sym.id, null),
        };
        Object.assign(c, detailsOf(ref && ref.details));
        if (ref && Array.isArray(ref.related) && ref.related.length) c.related = ref.related;
        constants.push(c);
    }

    // --- types (kind=="type") with methods / fields owned by each ---
    // Index members by owner once.
    const methodsByOwner = new Map();   // owner -> [method symbol, …]
    const fieldsByOwner = new Map();    // owner -> [field symbol, …]
    const takesInternal = (s) => (s.signatures || []).some(sig => /\binternal::/.test(sig.params || ''));
    for (const sym of REF_VALS) {
        if (sym.access === 'protected') continue;            // protected members are kept in the data but hidden from the public web reference
        if (sym.kind === 'method' && sym.owner) {
            if (isOperator(sym)) continue;                   // operators render via yaml schema
            if (takesInternal(sym)) continue;                // methods taking internal:: args (handle*, findHitNodeRecursive) aren't user-callable
            if (!methodsByOwner.has(sym.owner)) methodsByOwner.set(sym.owner, []);
            methodsByOwner.get(sym.owner).push(sym);
        } else if (sym.kind === 'field' && sym.owner) {
            if (!fieldsByOwner.has(sym.owner)) fieldsByOwner.set(sym.owner, []);
            fieldsByOwner.get(sym.owner).push(sym);
        }
    }

    // methods/static_methods carry English-only `desc` in the web artifact
    // (no desc_ja/desc_ko — matching the existing shape reference.js consumes).
    const memberEntry = (sym) => {
        const refId = sym.id;
        const ym = YML_METHOD.get(refId);
        const out = {
            name: sym.name,
            return: (sym.signatures[0] && sym.signatures[0].ret) ?? (ym && ym.return) ?? '',
            signatures: (sym.signatures.length ? sym.signatures : [{ params: '' }]).map(s => stripU(s.params || '')),
            desc: descTrio(refId, ym).desc,
        };
        const dep = mergeDeprecated(refId, ym);
        if (dep) out.deprecated = dep;
        return attachPlatforms(out, sym, ym);
    };

    const types = [];
    for (const sym of REF_VALS) {
        if (sym.kind !== 'type') continue;
        if (sym.owner) continue;                             // nested types (ChipSoundBundle::Entry, Font::PlacedGlyph) belong to their owner, not the flat type list
        const typeName = sym.name;
        const ym = YML_TYPE.get(typeName);
        const { desc, desc_ja, desc_ko } = descTrio(sym.id, ym);
        const typeData = { name: typeName, desc, keywords: refKeywords(sym.id, ym), desc_ja, desc_ko };
        if (examplesMap['type:' + typeName]) typeData.examples = examplesMap['type:' + typeName];
        if (Array.isArray(sym.related) && sym.related.length) typeData.related = sym.related;
        attachPlatforms(typeData, sym, ym);

        // constructor signatures: from the AST (reference-data) now; the yaml
        // sidecar only supplies an optional example snippet.
        if (sym.constructors && sym.constructors.length) {
            typeData.constructor = {
                signatures: sym.constructors.map(c => stripU(c.params)),
                snippet: ym && ym.constructor && ym.constructor.snippet,
            };
        }
        // properties (data members). Type comes from the C++ AST (reference-data).
        const fields = fieldsByOwner.get(typeName);
        if (fields && fields.length) {
            typeData.properties = fields.map(f => {
                const d = descTrio(f.id, null);
                return { name: f.name, type: f.type || '', desc: d.desc, desc_ja: d.desc_ja, desc_ko: d.desc_ko };
            });
        }
        // methods: reference-data members, split into instance vs static.
        const members = methodsByOwner.get(typeName) || [];
        const instance = members.filter(m => !m.static);
        const statics = members.filter(m => m.static);
        if (instance.length) typeData.methods = instance.map(memberEntry);
        if (statics.length) typeData.static_methods = statics.map(memberEntry);
        // operators: yaml schema only.
        const typeOps = mapOperators(typeName, ym); if (typeOps.length) typeData.operators = typeOps;
        types.push(typeData);
    }

    // --- enums (kind=="enum"; values from `members`, numbered/described via yaml) ---
    const enums = [];
    for (const sym of REF_VALS) {
        if (sym.kind !== 'enum') continue;
        if (sym.owner) continue;                             // nested enums (ScrollBar::Direction, CurveStyle::Mode, …) collide with bare names in the flat list; they belong to their owning type
        const ym = YML_ENUM.get(sym.name);
        const { desc, desc_ja, desc_ko } = descTrio(sym.id, ym);
        // members carry {name, value} from the AST; per-value prose from reference-data's value_desc.
        const vd = sym.value_desc || {};
        const memberList = (Array.isArray(sym.members) && sym.members.length)
            ? sym.members
            : Object.keys(vd).map((name, i) => ({ name, value: i }));
        const out = {
            name: sym.name,
            desc, keywords: refKeywords(sym.id, ym),
            values: memberList.map(m => {
                const d = vd[m.name] || {};
                return { name: m.name, value: m.value, desc: d.en || '', desc_ja: d.ja || '', desc_ko: d.ko || '' };
            }),
            desc_ja, desc_ko,
        };
        if (Array.isArray(sym.related) && sym.related.length) out.related = sym.related;
        const enumOps = mapOperators(sym.name, ym); if (enumOps.length) out.operators = enumOps;
        enums.push(out);
    }

    // --- macros (yaml only — not C++ symbols) ---
    const macros = (EXTRAS.macros || []).map(m => ({
        name: m.name,
        signature: m.signature || m.name,
        desc: m.description,
        desc_ja: m.description_ja || '',
        desc_ko: m.description_ko || '',
    }));

    return {
        version,
        lang: 'all',
        categories,
        constants,
        keywords: EXTRAS.keywords,
        types,
        enums,
        macros,
        colors: COLORS,
    };
}

function main() {
    console.log('Loading reference-data.json (canonical) + api-definition.yaml (aux structure)...');
    console.log(`  reference-data symbols: ${Object.keys(REF).length}`);
    const examplesMap = buildExamplesMap();
    console.log(`  Example links resolved for ${Object.keys(examplesMap).length} symbols`);

    const output = build(examplesMap);
    console.log(`  reference-data prose hits: ${refHits}  misses (yaml fallback): ${refMiss}`);

    const js = `// TrussC API Definition
// Complete C++ API reference (all functions, types, constants)
//
// AUTO-GENERATED by docs/scripts/emit-web.js
// Source: docs/reference/reference-data.json (canonical) + api-definition.yaml (aux)
// Do not edit directly.

const TrussCAPI = ${JSON.stringify(output, null, 4)};

// Export for different environments
if (typeof module !== 'undefined' && module.exports) {
    module.exports = TrussCAPI;
}
`;

    if (DRY) { console.log('\n[--dry] not writing.'); return; }
    fs.writeFileSync(TRUSSC_API_JS, js);
    console.log(`\nWritten: ${TRUSSC_API_JS}`);
    console.log(`  categories: ${output.categories.length}  types: ${output.types.length}  enums: ${output.enums.length}  constants: ${output.constants.length}  macros: ${output.macros.length}`);
}

main();
