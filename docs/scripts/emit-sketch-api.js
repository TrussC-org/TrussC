#!/usr/bin/env node
/**
 * emit-sketch-api.js — emit the TrussSketch editor's autocomplete data
 * (trussc.org/generated/trusssketch-api.js, global `TrussSketchAPI`) from
 * reference-data.json + colors.json. LUA flavor (TrussSketch runs Lua via
 * tcxLua): the advertised surface mirrors what luagen/luagen-types bind.
 *
 *   node emit-sketch-api.js            # writes trussc.org/generated/trusssketch-api.js
 *
 * Schema consumed by sketch-ui.js:
 *   TrussSketchAPI = {
 *     categories: [{ name, functions: [{ name, snippet, desc }] }],
 *     constants:  [{ name, value, desc }],
 *     types: [{ name, desc, constructor?: { snippet },
 *               properties?: [{ name, type, desc }],
 *               methods?:    [{ name, snippet, return, desc }],
 *               static_methods?: [{ name, snippet, return, desc }] }],
 *   }
 * Snippets use Monaco/CM6 syntax: `name(${1:arg}, ${2:arg})`.
 */
const fs = require('fs');
const path = require('path');

const RDJ = path.join(__dirname, '../reference/reference-data.json');
const COLORS = path.join(__dirname, '../reference/colors.json');
const OUT = path.join(__dirname, '../../../trussc.org/generated/trusssketch-api.js');

const data = JSON.parse(fs.readFileSync(RDJ, 'utf8'));
const colorsData = JSON.parse(fs.readFileSync(COLORS, 'utf8'));

// Actual Lua binding surface of the HAND-WRITTEN tcxLua usertypes (see the lib
// header): bound member names (end_fbo not end), no unbound members, plus the
// four value-typed Tween instances. Hand types are advertised from this instead
// of the raw C++ AST so completions never suggest unusable/unbound members.
const { getLuaBoundSurface, LUA_RESERVED, TWEEN_CTOR_PARAMS, TWEEN_METHOD_META } =
    require('./lib/lua-bound-surface.js');
const SURFACE = getLuaBoundSurface();
const HAND = SURFACE.types;
const dedup = a => [...new Set(a)];

// ---- mirrors of the luagen bindability rules (advertise only what's bound) --
const PRIM = new Set(['void','bool','char','short','int','long','float','double','unsigned','signed','size_t','int8_t','int16_t','int32_t','int64_t','uint8_t','uint16_t','uint32_t','uint64_t']);
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
// types NOT bound (mirror luagen-types EXCLUDE of internal::-poisoned ones)
const UNBOUND_TYPES = new Set(['Thread', 'SoundSource', 'Environment', 'VideoPlayerBase']);

const en = d => (d && d.en) || '';
const cleanName = n => (n || '').replace(/_+$/, '');

// snippet from args: required args only (defaults are optional in Lua)
function snippetOf(name, args) {
    let firstDefault = args.length;
    args.forEach((a, i) => { if (a.hasDefault && firstDefault === args.length) firstDefault = i; });
    const req = args.slice(0, firstDefault);
    if (!req.length) return `${name}()`;
    return `${name}(${req.map((a, i) => `\${${i + 1}:${cleanName(a.name) || 'a' + i}}`).join(', ')})`;
}
// first signature whose args are all bindable
function firstBindableSig(e) {
    for (const sig of (e.signatures || [])) {
        if (sig.tmpl) continue;
        const args = sig.args || [];
        if (args.every(argBindable)) return { sig, args };
    }
    return null;
}

// ---- free functions -> categories -------------------------------------------
const categories = {};
for (const id in data) {
    const e = data[id];
    if (e.kind !== 'func' || e.owner || e.ns || e.hidden || e.deprecated) continue;
    if (e.tparams && e.tparams.length) continue;
    const fb = firstBindableSig(e);
    if (!fb) continue;
    const cat = e.category || 'other';
    (categories[cat] ||= []).push({ name: e.name, snippet: snippetOf(e.name, fb.args), desc: en(e.description) });
}

// ---- types -------------------------------------------------------------------
const types = [];
for (const id in data) {
    const e = data[id];
    if (e.kind !== 'type' || e.owner || e.ns || e.hidden) continue;
    if (UNBOUND_TYPES.has(e.name)) continue;
    // Tween template: emitted as four value-typed instances below.
    if (e.name === 'Tween') continue;
    if (e.tparams && e.tparams.length && !(e.lua_bind && e.lua_bind.length)) continue;

    const t = { name: e.name, desc: en(e.description) };
    // bindable ctors (for named params), excluding the copy ctor.
    const bindCtors = (e.constructors || []).filter(c => (c.args || []).every(argBindable))
        .filter(c => { const a = c.args || []; return !(a.length === 1 && a[0].type.replace(/[&]/g, '').replace(/\bconst\b/g, '').trim() === e.name); });
    const properties = [], methods = [], statics = [];

    if (HAND.has(e.name)) {
        // Hand-written usertype — advertise ONLY the bound Lua surface.
        const surf = HAND.get(e.name);
        const cppMembers = new Map();
        for (const mid in data) { const m = data[mid]; if (m.owner === e.id) cppMembers.set(m.name, m); }

        // constructor snippet: the richest bound ctor, named via reference-data.
        if (surf.ctors.length) {
            const best = surf.ctors.reduce((x, y) => (y.count > x.count ? y : x));
            const rc = bindCtors.find(c => (c.args || []).length === best.count);
            const args = rc ? rc.args : Array.from({ length: best.count }, (_, i) => ({ name: 'a' + i }));
            t.constructor = { snippet: snippetOf(e.name, args) };
        }

        for (const mem of surf.members) {
            if (LUA_RESERVED.has(mem.lua)) continue;   // unusable Lua spelling; alias kept
            const m = mem.cpp ? cppMembers.get(mem.cpp) : null;
            if (m && m.kind === 'field') {
                properties.push({ name: mem.lua, type: m.type || '', desc: en(m.description) });
                continue;
            }
            const isStatic = !!(m && m.static);
            const fb = m ? firstBindableSig(m) : null;
            (isStatic ? statics : methods).push({
                name: mem.lua, snippet: snippetOf(mem.lua, fb ? fb.args : []),
                return: fb ? (fb.sig.ret || 'void') : '', desc: m ? en(m.description) : '',
            });
        }
    } else {
        // Generated usertype — reference-data IS the binding source.
        if (bindCtors.length) {
            const best = bindCtors.reduce((x, y) => ((y.args || []).length > (x.args || []).length ? y : x));
            t.constructor = { snippet: snippetOf(e.name, best.args || []) };
        }
        for (const mid in data) {
            const m = data[mid];
            if (m.owner !== e.id) continue;
            if (m.access && m.access !== 'public') continue;
            if (m.hidden || m.deprecated) continue;
            if (/^operator/.test(m.name)) continue;
            if (m.kind === 'field') {
                const ft = m.type || '';
                if (!ft || /\binternal::/.test(ft) || /\*/.test(ft) || /\[/.test(ft)) continue;
                properties.push({ name: m.name, type: ft, desc: en(m.description) });
            } else if (m.kind === 'method') {
                const fb = firstBindableSig(m);
                if (!fb) continue;
                (m.static ? statics : methods).push({
                    name: m.name, snippet: snippetOf(m.name, fb.args),
                    return: fb.sig.ret || 'void', desc: en(m.description),
                });
            }
        }
    }
    if (properties.length) t.properties = properties;
    if (methods.length) t.methods = methods;
    if (statics.length) t.static_methods = statics;
    types.push(t);
}

// ---- Tween: one usertype per value type (TweenFloat/Vec2/Vec3/Color) ----------
{
    const tw = SURFACE.tween;
    const tref = Object.values(data).find(x => x && x.kind === 'type' && x.name === 'Tween' && !x.owner && !x.ns) || {};
    // richest ctor snippet: Name(from, to, duration)
    const maxAr = Math.min(3, Math.max(0, ...tw.ctorArities));
    for (const vt of tw.valueTypes) {
        const tt = { name: vt.name, desc: en(tref.description) };
        tt.constructor = { snippet: snippetOf(vt.name, TWEEN_CTOR_PARAMS.slice(0, maxAr).map(n => ({ name: n }))) };
        tt.methods = tw.memberNames.map(nm => {
            const meta = TWEEN_METHOD_META[nm] || { params: [], ret: '' };
            const ret = meta.ret === 'self' ? vt.name : meta.ret === 'value' ? vt.value : (meta.ret || '');
            return { name: nm, snippet: snippetOf(nm, meta.params.map(p => ({ name: p }))), return: ret, desc: '' };
        });
        types.push(tt);
    }
}

// ---- enums as types (values via static access: BlendMode.Add) ----------------
for (const id in data) {
    const e = data[id];
    if (e.kind !== 'enum' || e.owner || e.ns || e.hidden) continue;
    if (!e.members || !e.members.length) continue;
    types.push({
        name: e.name, desc: en(e.description),
        static_methods: e.members.map(m => ({ name: m.name, snippet: m.name, return: e.name, desc: `= ${m.value}` })),
    });
}

// ---- colors table -------------------------------------------------------------
types.push({
    name: 'colors', desc: 'Named color constants',
    static_methods: colorsData.flatMap(g => g.items.map(i => ({ name: i.name, snippet: i.name, return: 'Color', desc: i.hex }))),
});

// ---- constants (kind:var, non-hidden — matches the generated bindings) --------
const constants = [];
for (const id in data) {
    const e = data[id];
    if (e.kind !== 'var' || e.owner || e.ns || e.hidden) continue;
    constants.push({ name: e.name, value: '', desc: en(e.description) });
}

// ---- emit ---------------------------------------------------------------------
const api = {
    categories: Object.keys(categories).sort().map(name => ({ name, functions: categories[name].sort((a, b) => a.name.localeCompare(b.name)) })),
    constants,
    types: types.sort((a, b) => a.name.localeCompare(b.name)),
};
const js = `// AUTO-GENERATED by TrussC/docs/scripts/emit-sketch-api.js — DO NOT EDIT.
// Lua-flavored TrussSketch editor API data (autocomplete). Source:
// docs/reference/reference-data.json + colors.json.
const TrussSketchAPI = ${JSON.stringify(api, null, 1)};
`;
fs.writeFileSync(OUT, js);
const fnCount = api.categories.reduce((n, c) => n + c.functions.length, 0);
console.log(`[emit-sketch-api] wrote ${path.relative(process.cwd(), OUT)}: ${fnCount} functions in ${api.categories.length} categories, ${api.types.length} types, ${constants.length} constants`);
