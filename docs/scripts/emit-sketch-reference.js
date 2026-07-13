#!/usr/bin/env node
/**
 * emit-sketch-reference.js — emit the TrussSketch REFERENCE PAGE data
 * (trussc.org/generated/trusssketch-ref.js, global `TrussCAPI`).
 *
 * This is a LUA-flavored twin of the C++ web reference emitted by emit-web.js.
 * It deliberately reuses the SAME global name (`TrussCAPI`) and the SAME output
 * schema so that trussc.org/reference/reference.js can render it UNCHANGED — the
 * only difference is the CONTENT:
 *
 *   • Surface  — only what tcxLua actually binds, mirroring the bindability rules
 *                in emit-sketch-api.js (argBindable + UNBOUND_TYPES + hidden /
 *                deprecated skips).
 *   • Signatures — rendered as Lua call forms (no C++ types / no `const&`):
 *                  free fns `drawRect(x, y, w, h)`, instance methods with colon
 *                  syntax (`mesh:draw()`), statics/fields with dot syntax
 *                  (`Color.fromHex`, `vec2.x`), enums accessed as `BlendMode.Add`.
 *   • Return types — mapped to Lua-facing names (number/boolean/string/(nothing)
 *                    or the TrussC type name).
 *   • Macros are dropped (no C preprocessor in Lua). A hand-written "Tasks"
 *     category advertises the playground host API (spawn / wait / forever).
 *
 * Prose (desc/desc_ja/desc_ko) + keywords are copied VERBATIM from reference-data
 * so search and i18n inherit for free.
 *
 * Usage:  node emit-sketch-reference.js
 * Output: ../../../trussc.org/generated/trusssketch-ref.js
 */

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

// --- paths -------------------------------------------------------------------
const REF_DATA = path.join(__dirname, '../reference/reference-data.json');
const CATEGORIES = path.join(__dirname, '../reference/categories.json');
const COLORS = path.join(__dirname, '../reference/colors.json');
const EXTRAS = path.join(__dirname, '../reference/extras.json');
const OUT = path.join(__dirname, '../../../trussc.org/generated/trusssketch-ref.js');

const REF = JSON.parse(fs.readFileSync(REF_DATA, 'utf8'));
const CATS = JSON.parse(fs.readFileSync(CATEGORIES, 'utf8'));
const CAT_BY_ID = new Map(CATS.map(c => [c.id, c]));
const colorsData = JSON.parse(fs.readFileSync(COLORS, 'utf8'));
const extras = JSON.parse(fs.readFileSync(EXTRAS, 'utf8'));

// ---- bindability rules (mirrors emit-sketch-api.js — advertise only what's bound)
const PRIM = new Set(['void', 'bool', 'char', 'short', 'int', 'long', 'float', 'double', 'unsigned', 'signed', 'size_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t']);
function argBindable(a) {
    if (a.isArray || a.isPointer) return false;
    if (/\binternal::/.test(a.type)) return false;
    if (a.isRef && !a.isConst && !/&&/.test(a.type)) {
        const base = a.type.replace(/[&*]/g, '').replace(/\bconst\b/g, '').trim();
        const allPrim = base.split(/\s+/).every(w => PRIM.has(w));
        const stdValue = base.includes('std::') || /\b(string|basic_string|vector|map|set|pair|tuple|function)\b/.test(base);
        if (allPrim || stdValue) return false;
    }
    return true;
}
// types NOT bound (mirror emit-sketch-api / luagen-types EXCLUDE of internal::-poisoned ones)
const UNBOUND_TYPES = new Set(['Thread', 'SoundSource', 'Environment', 'VideoPlayerBase']);

// ---- small helpers ----------------------------------------------------------
const en = d => (d && d.en) || '';
const ja = d => (d && d.ja) || '';
const ko = d => (d && d.ko) || '';
const cleanName = n => (n || '').replace(/_+$/, '');
// receiver variable name for instance calls: Vec2 -> vec2, Color -> color.
const recvName = t => t ? t.charAt(0).toLowerCase() + t.slice(1) : t;

// Map a C++ return/parameter type to a Lua-facing name.
//   float/double/int/size_t/... -> number, bool -> boolean,
//   std::string / fs::path -> string, void -> (nothing),
//   containers -> table / function, TrussC types keep their (cleaned) name.
const NUM = new Set(['char', 'short', 'int', 'long', 'float', 'double', 'unsigned', 'signed', 'size_t',
    'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t']);
function mapType(t) {
    if (t == null) return '(nothing)';
    let s = String(t).trim();
    // strip reference / pointer / const qualifiers
    s = s.replace(/&&/g, '').replace(/[&*]/g, '').replace(/\bconst\b/g, '').replace(/\bconstexpr\b/g, '').trim();
    if (s === '' || s === 'void') return '(nothing)';
    if (s === 'bool') return 'boolean';
    // strip a leading namespace chain (std::, tc::, fs::, ...) for the base check
    const bare = s.replace(/^(?:[A-Za-z_]\w*::)+/, '');
    if (bare === 'string' || bare === 'basic_string' || bare === 'path' || bare === 'string_view') return 'string';
    if (/^vector\b/.test(bare) || /^(map|set|unordered_map|unordered_set|array)\b/.test(bare)) return 'table';
    if (/^function\b/.test(bare)) return 'function';
    // primitive number words (possibly multi-word like "unsigned int")
    const words = s.split(/\s+/);
    if (words.every(w => NUM.has(w))) return 'number';
    // a TrussC / user type — keep its bare name (drop namespace + template args)
    return bare.replace(/<.*$/, '') || s;
}

// Convert a C++ default-value expression into a Lua-facing literal.
//   nullptr -> nil, std::string("x") -> "x", std::function<…>(…) -> nil,
//   empty std:: containers -> {}, X::Y -> X.Y, 1.0f -> 1.0.
function luaDefault(d) {
    let s = String(d).trim();
    if (s === 'nullptr' || s === 'NULL') return 'nil';
    let m = s.match(/^std::string\((.*)\)$/);
    if (m) { const inner = m[1].trim(); return inner === '' ? '""' : inner; }
    m = s.match(/^"(.*)"s?$/);
    if (m) return `"${m[1]}"`;
    if (/^std::function\b/.test(s)) return 'nil';
    if (/^std::(vector|map|set|unordered_map|unordered_set|array)\b.*\(\s*\)$/.test(s)) return '{}';
    if (/^std::.*\(\s*\)$/.test(s)) return 'nil';
    if (/^-?\d*\.?\d+f$/.test(s)) return s.replace(/f$/, '');   // strip float suffix
    return s.replace(/::/g, '.');                               // X::Y (enum/const) -> X.Y
}

// Lua parameter string from a signature's args (no types). Optional args (those
// with a C++ default) render Lua-style as `name = default`.
function luaParams(args, { withDefaults = true } = {}) {
    return (args || []).map((a, i) => {
        const nm = cleanName(a.name) || ('a' + i);
        if (withDefaults && a.hasDefault && a.default != null) return `${nm} = ${luaDefault(a.default)}`;
        return nm;
    }).join(', ');
}

// A signature is bindable when it is not a template and every arg is bindable.
function sigBindable(sig) {
    if (!sig || sig.tmpl) return false;
    return (sig.args || []).every(argBindable);
}

function getVersion() {
    try {
        return execSync('git describe --tags --abbrev=0', { cwd: path.join(__dirname, '../..') })
            .toString().trim();
    } catch { return 'unknown'; }
}

// =============================================================================
// Free functions -> categories (grouped + ordered via categories.json)
// =============================================================================
const OTHER_ID = '__other__';
const funcsByCat = new Map();   // catId -> [func entry, …]
for (const id in REF) {
    const e = REF[id];
    if (e.kind !== 'func' || e.owner || e.ns || e.hidden || e.deprecated) continue;
    if (e.tparams && e.tparams.length) continue;
    if (/^operator/.test(e.name)) continue;
    const bindSigs = (e.signatures || []).filter(sigBindable);
    if (!bindSigs.length) continue;

    const catId = e.category && CAT_BY_ID.has(e.category) ? e.category : OTHER_ID;
    if (!funcsByCat.has(catId)) funcsByCat.set(catId, []);
    const bucket = funcsByCat.get(catId);
    // one entry per bindable overload signature (legacy-flattened shape)
    for (const sig of bindSigs) {
        bucket.push({
            name: e.name,
            params: luaParams(sig.args, { withDefaults: false }),
            params_typed: luaParams(sig.args, { withDefaults: true }),
            return_type: mapType(sig.ret),
            desc: en(e.description),
            keywords: Array.isArray(e.keywords) ? e.keywords : [],
            desc_ja: ja(e.description),
            desc_ko: ko(e.description),
        });
    }
}

// Emit categories in categories.json order, then leftover ids, then Other.
const orderedCatIds = [
    ...CATS.slice().sort((a, b) => (a.order ?? 1e9) - (b.order ?? 1e9)).map(c => c.id).filter(id => funcsByCat.has(id)),
    ...[...funcsByCat.keys()].filter(id => id !== OTHER_ID && !CAT_BY_ID.has(id)),
    ...(funcsByCat.has(OTHER_ID) ? [OTHER_ID] : []),
];
const catDisplay = (id) => {
    if (id === OTHER_ID) return { name: 'Other', name_ja: 'その他', name_ko: '기타' };
    const c = CAT_BY_ID.get(id);
    return c ? { name: c.name, name_ja: c.name_ja || '', name_ko: c.name_ko || '' } : { name: id, name_ja: '', name_ko: '' };
};

const categories = [];

// Hand-written "Tasks" category — the playground host API (cooperative tasks).
// These functions are NOT in reference-data; they are provided by the sketch runtime.
categories.push({
    name: 'Tasks',
    name_ja: 'タスク',
    name_ko: '태스크',
    functions: [
        {
            name: 'spawn', params: 'fn', params_typed: 'fn', return_type: '(nothing)',
            desc: 'Start a cooperative task; runs until fn returns.',
            desc_ja: '協調タスクを開始。fnが終わるまで動く',
            desc_ko: '협조 태스크를 시작; fn이 반환될 때까지 실행됨.',
            keywords: ['coroutine', 'task', 'async', 'thread', 'concurrent'],
        },
        {
            name: 'wait', params: 'seconds', params_typed: 'seconds', return_type: '(nothing)',
            desc: 'Pause the current task for the given number of seconds.',
            desc_ja: '現在のタスクを指定秒だけ一時停止する',
            desc_ko: '현재 태스크를 주어진 초만큼 일시정지한다.',
            keywords: ['sleep', 'delay', 'pause', 'yield', 'task'],
        },
        {
            name: 'forever', params: 'fn', params_typed: 'fn', return_type: '(nothing)',
            desc: 'Repeat fn every frame, with an implicit wait(0) between iterations.',
            desc_ja: '毎フレームfnを繰り返す（暗黙のwait(0)付き）',
            desc_ko: '매 프레임 fn을 반복하며, 반복마다 암묵적으로 wait(0)을 넣는다.',
            keywords: ['loop', 'repeat', 'every', 'frame', 'task'],
        },
    ],
});

for (const catId of orderedCatIds) {
    const functions = funcsByCat.get(catId);
    if (!functions || !functions.length) continue;
    const d = catDisplay(catId);
    categories.push({ name: d.name, functions, name_ja: d.name_ja, name_ko: d.name_ko });
}

// =============================================================================
// Types (with Lua-flavored constructor / properties / methods / static methods)
// =============================================================================
const types = [];
for (const id in REF) {
    const e = REF[id];
    if (e.kind !== 'type' || e.owner || e.ns || e.hidden) continue;
    if (UNBOUND_TYPES.has(e.name)) continue;
    if (e.tparams && e.tparams.length && !(e.lua_bind && e.lua_bind.length)) continue;

    const typeName = e.name;
    const recv = recvName(typeName);
    const t = {
        name: typeName,
        desc: en(e.description),
        keywords: Array.isArray(e.keywords) ? e.keywords : [],
        desc_ja: ja(e.description),
        desc_ko: ko(e.description),
    };
    if (Array.isArray(e.related) && e.related.length) t.related = e.related;

    // constructor: every bindable ctor except the copy ctor (single self-typed arg).
    const ctors = (e.constructors || []).filter(c => (c.args || []).every(argBindable))
        .filter(c => { const a = c.args || []; return !(a.length === 1 && a[0].type.replace(/[&]/g, '').replace(/\bconst\b/g, '').trim() === typeName); });
    if (ctors.length) {
        // Type(...) call form (sol __call). Render every signature.
        t.constructor = { signatures: ctors.map(c => luaParams(c.args)) };
    }

    const properties = [], methods = [], statics = [];
    for (const mid in REF) {
        const m = REF[mid];
        if (m.owner !== e.id) continue;
        if (m.access && m.access !== 'public') continue;
        if (m.hidden || m.deprecated) continue;
        if (/^operator/.test(m.name)) continue;
        if (m.kind === 'field') {
            const ft = m.type || '';
            if (!ft || /\binternal::/.test(ft) || /\*/.test(ft) || /\[/.test(ft)) continue;
            // fields accessed with dot syntax on an instance: `vec2.x`
            properties.push({
                name: `${recv}.${m.name}`,
                type: mapType(ft),
                desc: en(m.description),
                desc_ja: ja(m.description),
                desc_ko: ko(m.description),
            });
        } else if (m.kind === 'method') {
            const bindSigs = (m.signatures || []).filter(sigBindable);
            if (!bindSigs.length) continue;
            const isStatic = !!m.static;
            // instance -> colon syntax `recv:method`; static -> dot syntax `Type.method`
            const dispName = isStatic ? `${typeName}.${m.name}` : `${recv}:${m.name}`;
            (isStatic ? statics : methods).push({
                name: dispName,
                return: mapType(bindSigs[0].ret),
                signatures: bindSigs.map(s => luaParams(s.args)),
                desc: en(m.description),
                desc_ja: ja(m.description),
                desc_ko: ko(m.description),
            });
        }
    }
    if (properties.length) t.properties = properties;
    if (methods.length) t.methods = methods;
    if (statics.length) t.static_methods = statics;
    types.push(t);
}
types.sort((a, b) => a.name.localeCompare(b.name));

// =============================================================================
// Enums (values accessed in Lua as `BlendMode.Add`)
// =============================================================================
const enums = [];
for (const id in REF) {
    const e = REF[id];
    if (e.kind !== 'enum' || e.owner || e.ns || e.hidden) continue;
    if (!e.members || !e.members.length) continue;
    const out = {
        name: e.name,
        desc: en(e.description),
        keywords: Array.isArray(e.keywords) ? e.keywords : [],
        values: e.members.map(m => ({
            name: m.name, value: m.value,
            desc: '', desc_ja: '', desc_ko: '',
        })),
        desc_ja: ja(e.description),
        desc_ko: ko(e.description),
    };
    if (Array.isArray(e.related) && e.related.length) out.related = e.related;
    enums.push(out);
}
enums.sort((a, b) => a.name.localeCompare(b.name));

// =============================================================================
// Constants (kind:var, non-hidden — matches the generated bindings)
// =============================================================================
const constants = [];
for (const id in REF) {
    const e = REF[id];
    if (e.kind !== 'var' || e.owner || e.ns || e.hidden) continue;
    constants.push({
        name: e.name,
        value: extras.constants[e.id] ?? extras.constants[e.name] ?? '',
        desc: en(e.description),
        desc_ja: ja(e.description),
        desc_ko: ko(e.description),
        keywords: Array.isArray(e.keywords) ? e.keywords : [],
    });
}
constants.sort((a, b) => a.name.localeCompare(b.name));

// =============================================================================
// Emit — SAME global (`TrussCAPI`) + SAME schema as generated/trussc-api.js.
// `macros` is intentionally omitted (empty) — Lua has no preprocessor.
// =============================================================================
const output = {
    version: getVersion() + ' (Lua)',
    lang: 'lua',   // renderer switches scope separator ('.' not '::') on this
    categories,
    constants,
    keywords: extras.keywords,
    types,
    enums,
    macros: [],
    colors: colorsData,
};

const js = `// TrussSketch Reference (Lua)
// Lua-flavored twin of the C++ API reference. SAME global (\`TrussCAPI\`) and
// schema as generated/trussc-api.js so /reference/reference.js renders it unchanged.
//
// AUTO-GENERATED by docs/scripts/emit-sketch-reference.js
// Source: docs/reference/reference-data.json (bound surface only) + colors.json.
// Do not edit directly.

const TrussCAPI = ${JSON.stringify(output, null, 4)};

// Export for different environments
if (typeof module !== 'undefined' && module.exports) {
    module.exports = TrussCAPI;
}
`;

fs.writeFileSync(OUT, js);
const fnCount = categories.reduce((n, c) => n + c.functions.length, 0);
console.log(`[emit-sketch-reference] wrote ${path.relative(process.cwd(), OUT)}`);
console.log(`  version: ${output.version}`);
console.log(`  categories: ${categories.length}  functions(overload rows): ${fnCount}  types: ${types.length}  enums: ${enums.length}  constants: ${constants.length}  colorGroups: ${colorsData.length}`);
