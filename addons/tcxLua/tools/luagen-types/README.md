# luagen-types (Phase 2 — usertype generation)

Generates tcxLua Sol2 **usertype** bindings (constructors, properties, methods,
static methods, operators) from `docs/reference/reference-data.json`. Pairs with
`luagen/` (Phase 1 = free functions).

```bash
node luagen-types.js ../../../../docs/reference/reference-data.json > /tmp/gentypes.cpp
```

## Status (WIP) — 96 / 119 types compile clean (isolation syntax-only witness)

Value/data types (Vec2/Vec3/Color/Rect/Mat/Quaternion/…) generate & compile fully.
23 types fail on 3 reference-data-side issues — see Obsidian
"luagen Phase2 usertype 生成 — reference-data への要望":

1. **no access modifier** — protected members can't be filtered (~13 types). Needs
   `access` on member entries. Highest impact.
2. **nested types recorded top-level** — `Font::PlacedGlyph` stored as `PlacedGlyph`
   without enclosing scope (~5 types). Needs `enclosing` / fully-qualified id.
3. **non-copyable types** — generated copy ctors / value returns hit deleted ctors
   (~11 types). Needs `copyable:false`, or generator drops copy/move ctors.

Operators map to `sol::meta_function` (+ - * / [] == < <=); compound-assign/=/<</!=
are skipped (no Lua equivalent). Custom-glue types (Json/Xml) stay hand-written.
Not yet integrated into setBindings — blocked on the above.
