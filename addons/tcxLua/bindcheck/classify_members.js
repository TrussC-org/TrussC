#!/usr/bin/env node
// Split the registered usertype members into the ones bindcheck can safely probe
// and the ones it cannot, and emit both as Lua tables.
//
// WHY a classifier is needed at all: sol2 keeps a usertype's members inside
// `__sol.storage`, so from Lua they are neither enumerable nor rawget-able --
// probing `getmetatable(T)` only yields sol2's own keys (__index, __newindex,
// __sol.storage, ...). Indexing the TYPE table works for a member FUNCTION
// (`Vec2.length` hands back the function), but for a member VARIABLE sol2 runs
// the property getter immediately with the type table as `self`, reads it as a
// userdata pointer, and SEGFAULTS -- past a pcall, since it is not a Lua error.
//
// Which of the two a name is cannot be told from the C++ registration: fields and
// methods are both emitted as `t["name"] = &trussc::T::name`. reference-data.json
// -- the AST-derived file the bindings themselves are generated from -- does know,
// so the classification is read from there rather than guessed from the source.
//
// stdin: "LuaTypeName<TAB>CppOwner<TAB>member" lines
// argv:  path to reference-data.json
const fs = require('fs');
const data = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));

// A TC_PLATFORMS member is emitted inside an #if, so on any other platform it is
// never compiled and never reachable -- reading the registration out of the
// SOURCE the way this script does would otherwise demand it anyway. That is not
// hypothetical: the first run of the method check reported exactly one miss,
// VideoWriter.submitFrame, which is TC_PLATFORMS("macos"). Same platform names
// luagen-types.js keys its guards on.
const HOST = process.platform === 'darwin' ? 'macos' : process.platform === 'win32' ? 'windows' : 'linux';
const offHost = (e) => !!(e && e.platforms && e.platforms.length && !e.platforms.includes(HOST));
let skippedPlatform = 0;

// (owner, member) -> kind, keyed on the C++ owner as reference-data spells it.
// Enum VALUES live in the owning enum's `members` array rather than as symbols of
// their own, so they are folded in here too: an enum value is bound with
// sol::var and reads back off the type table (EaseType.Linear), so it is safe to
// probe -- but it is not a method, and lumping it in with methods would make the
// report lie about what was checked.
const kindOf = new Map();
for (const id in data) {
    const e = data[id];
    if (e.owner && (e.kind === 'method' || e.kind === 'field')) kindOf.set(`${e.owner}::${e.name}`, e.kind);
    if (e.kind === 'enum') for (const m of (e.members || [])) kindOf.set(`${id}::${m.name}`, 'enumvalue');
}

const methods = new Map(), fields = new Map(), enums = new Map(), unknown = [];
for (const line of fs.readFileSync(0, 'utf8').split('\n')) {
    if (!line.trim()) continue;
    const [luaName, cppOwner, member] = line.split('\t');
    if (offHost(data[`${cppOwner}::${member}`])) { skippedPlatform++; continue; }
    const kind = kindOf.get(`${cppOwner}::${member}`);
    // Still unknown: hand-written glue binding something the AST does not record
    // as a plain member (a lambda, a Lua-keyword rename like end_fbo). Probe it
    // as a method -- indexing is safe there, and a real absence still shows up.
    if (!kind) unknown.push(`${luaName}.${member}`);
    const bucket = kind === 'field' ? fields : kind === 'enumvalue' ? enums : methods;
    if (!bucket.has(luaName)) bucket.set(luaName, new Set());
    bucket.get(luaName).add(member);
}

function emit(label, map) {
    const out = [`  ${label} = {`];
    for (const k of [...map.keys()].sort())
        out.push(`    ${k} = { ${[...map.get(k)].sort().map(m => `"${m}"`).join(', ')} },`);
    out.push('  },');
    return out.join('\n');
}
process.stdout.write([emit('methods', methods), emit('enum_values', enums), emit('fields', fields)].join('\n') + '\n');
const n = (m) => [...m.values()].reduce((a, s) => a + s.size, 0);
process.stderr.write(`[classify] probed: methods ${n(methods)}/${methods.size} types, enum values ${n(enums)}/${enums.size} types`
    + ` | NOT probed: fields ${n(fields)}/${fields.size} types (sol2 segfaults reading a field off the type table)`
    + ` | skipped off-platform ${skippedPlatform}`
    + ` | unclassified ${unknown.length}${unknown.length ? ' (probed as methods): ' + unknown.slice(0, 6).join(' ') + (unknown.length > 6 ? ' …' : '') : ''}\n`);
