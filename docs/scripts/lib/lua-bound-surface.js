/**
 * lua-bound-surface.js — extract the ACTUAL Lua binding surface of the
 * HAND-WRITTEN tcxLua usertypes, so the sketch emitters advertise what Lua can
 * really call (not the raw C++ AST).
 *
 * Why: hand usertypes in addons/tcxLua/src/tcxLua.cpp register members under
 * LUA names that can differ from the C++ member (`fbo["end_fbo"] = &Fbo::end`
 * because `end` is a Lua reserved word) and DROP members that reference-data
 * lists but nothing binds (`Fbo::lifetimeToken`). Driving the emitters off
 * reference-data alone therefore emits methods that are either unusable Lua
 * (`fbo:end`) or simply not there. This module parses the binding sources —
 * the same files bindcheck/regen_expected.sh scans — and returns, per hand
 * usertype, the registered LUA names + the C++ member each one targets, plus
 * the Tween template surface (instantiated as TweenFloat/Vec2/Vec3/Color).
 *
 * Parsing approach (mirrors regen_expected.sh, extended to member level):
 *   • `new_usertype<CppType>("LuaName", …)` opens a usertype block; the call
 *     args yield the Lua name (first arg), the sol::constructors<…> list, and —
 *     for the INLINE registration style — `"member", value` pairs.
 *   • The POST-ASSIGNMENT style (`var["member"] = value;`) is collected by
 *     scanning `var["…"] = …;` statements for the block's variable.
 *   • For each registered member the referenced C++ name is recovered from
 *     `&Type::member` (pointer) or the lambda body's `recv.method(` call, so
 *     prose can be matched from reference-data by C++ name.
 *   • sol meta_functions / call_constructor / base_classes are not members.
 *
 * This module does NO reference-data lookup and NO rendering — it only reports
 * the raw bound surface. The emitters join it against reference-data.
 */
'use strict';
const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '../../../addons/tcxLua/src');
const CPP = path.join(SRC, 'tcxLua.cpp');
const HDR = path.join(SRC, 'tcxLua.h');

// Lua reserved words — a usertype member registered under one of these is
// unusable as `obj:word()` / `Type.word` in Lua (syntax error), so a usable
// alias (e.g. end_fbo) is always registered alongside. We drop the reserved
// spelling and keep the alias.
const LUA_RESERVED = new Set(['and', 'break', 'do', 'else', 'elseif', 'end',
    'false', 'for', 'function', 'goto', 'if', 'in', 'local', 'nil', 'not',
    'or', 'repeat', 'return', 'then', 'until', 'while']);

// --- low-level scanning helpers ---------------------------------------------

// Return the index of the ')' that matches the '(' at `open` (paren-aware,
// string-aware). Angle brackets are NOT tracked here (irrelevant for paren
// matching, and `->` would corrupt an angle counter).
function matchParen(s, open) {
    let depth = 0;
    for (let i = open; i < s.length; i++) {
        const c = s[i];
        if (c === '"' || c === "'") { i = skipString(s, i); continue; }
        if (c === '(') depth++;
        else if (c === ')') { if (--depth === 0) return i; }
    }
    return -1;
}

// Return the index of the '>' that matches the '<' at `open`. Guards against
// the arrow `->` (a '>' preceded by '-' is not an angle close).
function matchAngle(s, open) {
    let depth = 0;
    for (let i = open; i < s.length; i++) {
        const c = s[i];
        if (c === '"' || c === "'") { i = skipString(s, i); continue; }
        if (c === '<') depth++;
        else if (c === '>' && s[i - 1] !== '-') { if (--depth === 0) return i; }
    }
    return -1;
}

// Strip C/C++ comments (string-literal aware) so commented-out bindings — e.g.
// the disabled StdPath usertype and `// FIXME` notes — are not parsed as real
// registrations. Mirrors regen_expected.sh's `grep -vE '^\s*//'` intent.
function stripComments(s) {
    let out = '';
    for (let i = 0; i < s.length; i++) {
        const c = s[i], n = s[i + 1];
        if (c === '"' || c === "'") { const e = skipString(s, i); out += s.slice(i, e + 1); i = e; continue; }
        if (c === '/' && n === '/') { while (i < s.length && s[i] !== '\n') i++; out += '\n'; continue; }
        if (c === '/' && n === '*') { i += 2; while (i < s.length && !(s[i] === '*' && s[i + 1] === '/')) i++; i++; out += ' '; continue; }
        out += c;
    }
    return out;
}

// Advance past a string/char literal starting at `i` (i points at the quote).
function skipString(s, i) {
    const q = s[i];
    for (let j = i + 1; j < s.length; j++) {
        if (s[j] === '\\') { j++; continue; }
        if (s[j] === q) return j;
    }
    return s.length;
}

// Split a comma-separated arg list at depth 0. Tracks () [] {} and <>
// (with the `->` guard), and skips string literals.
function splitTopLevel(s) {
    const out = [];
    let depth = 0, angle = 0, start = 0;
    for (let i = 0; i < s.length; i++) {
        const c = s[i];
        if (c === '"' || c === "'") { i = skipString(s, i); continue; }
        else if (c === '(' || c === '[' || c === '{') depth++;
        else if (c === ')' || c === ']' || c === '}') depth--;
        else if (c === '<') angle++;
        else if (c === '>' && s[i - 1] !== '-') { if (angle > 0) angle--; }
        else if (c === ',' && depth === 0 && angle === 0) {
            out.push(s.slice(start, i)); start = i + 1;
        }
    }
    const tail = s.slice(start);
    if (tail.trim() !== '') out.push(tail);
    return out.map(x => x.trim());
}

const isStringLit = t => /^"([^"]*)"$/.test(t.trim());
const unquote = t => t.trim().replace(/^"/, '').replace(/"$/, '');

// Recover the C++ member name a bound value expression refers to:
//   &Type::member                     -> member   (pointer-to-member)
//   [](T& f){ return f.method(...); } -> method   (lambda body call)
// Returns null when nothing recognizable is referenced (pure workaround
// lambdas with no member call).
function extractCpp(expr) {
    // pointer-to-member: require at least one `Ident::` so lambda params like
    // `Fbo& f` (a reference `&`, not a member pointer) are not misread.
    let m = expr.match(/&\s*(?:[A-Za-z_]\w*\s*::\s*)+([A-Za-z_]\w*)/);
    if (m) return m[1];
    // lambda body: first `recv.method(` call (skips `lua->create_table`, `->`).
    m = expr.match(/\b[A-Za-z_]\w*\s*\.\s*([A-Za-z_]\w*)\s*\(/);
    if (m) return m[1];
    return null;
}

// Read from `i` up to the depth-0 ';' (statement end). Paren/brace/bracket and
// string aware; angle brackets are ignored (`->` would corrupt them).
function readStatement(s, i) {
    let depth = 0;
    for (; i < s.length; i++) {
        const c = s[i];
        if (c === '"' || c === "'") { i = skipString(s, i); continue; }
        else if (c === '(' || c === '[' || c === '{') depth++;
        else if (c === ')' || c === ']' || c === '}') depth--;
        else if (c === ';' && depth === 0) return s.slice(0, i);
    }
    return s;
}

// Parse a `sol::constructors< … >` list into arg-type arrays, excluding the
// copy/move constructor (a single self-typed arg). `contents` is the text
// inside the angle brackets.
function parseCtorList(contents, cppType) {
    const ctors = [];
    for (const entry of splitTopLevel(contents)) {
        // `Type` (no parens) == default ctor; `Type(a, b)` -> capture args.
        const paren = entry.indexOf('(');
        let args = [];
        if (paren >= 0) {
            const close = matchParen(entry, paren);
            const inner = entry.slice(paren + 1, close).trim();
            if (inner !== '') args = splitTopLevel(inner);
        }
        // copy/move: single arg whose base type is the usertype itself.
        if (args.length === 1) {
            const base = args[0].replace(/[&*]/g, '').replace(/\bconst\b/g, '')
                .replace(/\b\w+$/, '').replace(/[<>].*$/, '').trim() || args[0];
            const baseType = args[0].replace(/[&*]/g, '').replace(/\bconst\b/g, '').trim().split(/\s+/)[0];
            if (baseType === cppType || base === cppType || /&&/.test(args[0])) continue;
        }
        ctors.push({ count: args.length, args });
    }
    return ctors;
}

// Parse inline `"member", value` pairs and the first sol::constructors from a
// new_usertype(...) call's arg text. args[0] is the Lua type name (skipped).
function parseCallArgs(callArgs, cppType) {
    const parts = splitTopLevel(callArgs);
    const members = [];
    let ctors = [];
    for (let i = 1; i < parts.length; i++) {
        const p = parts[i];
        if (isStringLit(p)) {
            const value = parts[i + 1] || '';
            members.push({ lua: unquote(p), cpp: extractCpp(value) });
            i++;   // consume the value token
            continue;
        }
        if (!ctors.length && /^sol::constructors\s*</.test(p)) {
            const open = p.indexOf('<');
            const close = matchAngle(p, open);
            ctors = parseCtorList(p.slice(open + 1, close), cppType);
        }
    }
    return { members, ctors };
}

// --- hand usertypes ----------------------------------------------------------

function parseHandTypes(text) {
    const types = new Map();
    // Optional `sol::usertype<T> VAR =` prefix captures the block variable used
    // by the post-assignment style.
    const re = /(?:sol::usertype<[^>]+>\s+(\w+)\s*=\s*)?lua->new_usertype<([^>]*)>\s*\(/g;
    let m;
    while ((m = re.exec(text))) {
        const varName = m[1];
        const cppType = m[2].trim();
        const open = m.index + m[0].length - 1;   // index of '('
        const close = matchParen(text, open);
        if (close < 0) continue;
        const callArgs = text.slice(open + 1, close);
        const parts = splitTopLevel(callArgs);
        if (!parts.length || !isStringLit(parts[0])) continue;
        const luaName = unquote(parts[0]);

        const { members, ctors } = parseCallArgs(callArgs, cppType);
        if (varName) members.push(...collectAssignments(text, varName));

        // De-dup members by Lua name (last write wins, matching C++ semantics).
        const byLua = new Map();
        for (const mem of members) byLua.set(mem.lua, mem);
        types.set(luaName, { cppType, ctors, members: [...byLua.values()] });
    }
    return types;
}

// Collect `VAR["member"] = value;` post-assignments for one block variable.
function collectAssignments(text, varName) {
    const members = [];
    const re = new RegExp(varName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') +
        '\\s*\\[\\s*"([^"]+)"\\s*\\]\\s*=', 'g');
    let m;
    while ((m = re.exec(text))) {
        const expr = readStatement(text, re.lastIndex);
        members.push({ lua: m[1], cpp: extractCpp(expr.slice(re.lastIndex)) });
    }
    return members;
}

// --- Tween template ----------------------------------------------------------

// The Tween usertype is registered by a template (defineTween in tcxLua.h) and
// instantiated four times in tcxLua.cpp. We read the bound member NAMES + ctor
// overloads from the template, and the value types from the instantiations.
function parseTween(cppText, hdrText) {
    // instantiations: defineTween<Tween<float>, float>(lua, "TweenFloat")
    const valueTypes = [];
    const reInst = /defineTween<[^,]+,\s*([\w:]+)\s*>\s*\(\s*lua\s*,\s*"([^"]+)"/g;
    let m;
    while ((m = reInst.exec(cppText))) valueTypes.push({ name: m[2], value: m[1].trim() });

    // template body: the new_usertype call inside defineTween.
    const nu = hdrText.indexOf('lua->new_usertype<TweenT>');
    let memberNames = [], ctorArities = [];
    if (nu >= 0) {
        const open = hdrText.indexOf('(', nu);
        const close = matchParen(hdrText, open);
        const callArgs = hdrText.slice(open + 1, close);
        // every string literal in the template call is a bound member name
        // (the first call arg is the `name` VARIABLE, not a literal).
        const parts = splitTopLevel(callArgs);
        for (let i = 0; i < parts.length; i++) {
            if (isStringLit(parts[i])) { memberNames.push(unquote(parts[i])); i++; }
        }
        // ctor overloads -> arities (arg counts), for named-param rendering.
        const ci = callArgs.indexOf('sol::constructors<');
        if (ci >= 0) {
            const a = callArgs.indexOf('<', ci);
            const b = matchAngle(callArgs, a);
            for (const entry of splitTopLevel(callArgs.slice(a + 1, b))) {
                const paren = entry.indexOf('(');
                if (paren < 0) { ctorArities.push(0); continue; }
                const inner = entry.slice(paren + 1, matchParen(entry, paren)).trim();
                ctorArities.push(inner === '' ? 0 : splitTopLevel(inner).length);
            }
        }
    }
    // unique arities, ascending
    ctorArities = [...new Set(ctorArities)].sort((x, y) => x - y);
    return { valueTypes, memberNames, ctorArities };
}

// --- public API --------------------------------------------------------------

function getLuaBoundSurface() {
    const cppText = stripComments(fs.readFileSync(CPP, 'utf8'));
    const hdrText = stripComments(fs.readFileSync(HDR, 'utf8'));
    return {
        types: parseHandTypes(cppText),
        tween: parseTween(cppText, hdrText),
    };
}

// Rendering metadata for the Tween usertype. reference-data's `Tween` entry has
// prose but ZERO members (the members live on the template), so signatures /
// returns are supplied here from tcTween.h. `ret`: 'self' -> the Tween* type
// (chainable setters), 'value' -> the tween's value type (float/Vec2/…).
const TWEEN_CTOR_PARAMS = ['from', 'to', 'duration', 'ease', 'mode'];
const TWEEN_METHOD_META = {
    from: { params: ['value'], ret: 'self' }, to: { params: ['value'], ret: 'self' },
    duration: { params: ['seconds'], ret: 'self' }, ease: { params: ['type', 'mode'], ret: 'self' },
    loop: { params: ['count'], ret: 'self' }, yoyo: { params: ['enable'], ret: 'self' },
    delay: { params: ['seconds'], ret: 'self' }, start: { params: [], ret: 'self' },
    pause: { params: [], ret: 'self' }, resume: { params: [], ret: 'self' },
    reset: { params: [], ret: 'self' }, finish: { params: [], ret: 'self' },
    getValue: { params: [], ret: 'value' }, getProgress: { params: [], ret: 'number' },
    getElapsed: { params: [], ret: 'number' }, getDuration: { params: [], ret: 'number' },
    isPlaying: { params: [], ret: 'boolean' }, isComplete: { params: [], ret: 'boolean' },
    getStart: { params: [], ret: 'value' }, getEnd: { params: [], ret: 'value' },
    getLoopCount: { params: [], ret: 'number' },
};

module.exports = { getLuaBoundSurface, LUA_RESERVED, TWEEN_CTOR_PARAMS, TWEEN_METHOD_META };
