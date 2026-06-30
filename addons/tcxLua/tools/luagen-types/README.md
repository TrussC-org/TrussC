# luagen-types (Phase 2 — usertype generation)

Generates tcxLua Sol2 **usertype** bindings (constructors, properties, methods,
static methods, operators) from `docs/reference/reference-data.json`. Pairs with
`luagen/` (Phase 1 = free functions).

```bash
node luagen-types.js ../../../../docs/reference/reference-data.json > /tmp/gentypes.cpp
```

## Status — 109 types compile clean (isolation syntax-only witness), 0 errors

Generated from reference-data, verified by compiling against the real headers.
Derives everything from the data:
- **constructors** from the type's `constructors[]` (copy/move ctors dropped — non-copyable types)
- **operators** `+ - * / [] ==` (and `< <=`) → `sol::meta_function`; compound-assign / `=` / `<<` / `!=` skipped (no Lua equivalent)
- **properties** = fields, **methods** / **static methods** = member fns
- skips **protected/private** members (reference-data `access`), **template** members,
  **unbindable** args (out-param / pointer / array), and **`internal::`** incomplete types
- **trailing-default arity expansion** (Lua optional args), like luagen Phase 1
- **reference returns** use `-> decltype(auto)` (chaining `*this`, non-copyable refs)
- qualifies TrussC types (`trussc::Vec2`), nested types (`trussc::SoundSource::Kind`),
  and falls back to owner-nested for unrecorded member typedefs (`Node::Ptr`)

### Excluded (7) — pending reference-data, see Obsidian handoff
`Serial Node Thread SoundSource PlayingSound Environment VideoPlayerBase` expose
incomplete `internal::` types through **untyped fields** (reference-data fields carry
no `type`) and/or typedef'd returns. `Json`/`Xml` keep custom Lua glue (hand-written).

Not yet wired into setBindings (adoption = replace the hand value-type usertypes).
