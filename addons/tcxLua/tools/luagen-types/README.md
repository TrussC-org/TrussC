# luagen-types (Phase 2 — usertype generation)

Generates tcxLua Sol2 **usertype** bindings (constructors, properties, methods,
static methods, operators), **enums** (32, same usertype+`sol::var` style as the
old hand bindings), and the **`colors` constant table** (from `colors.json`, 149
colors) — all from `docs/reference/`. Pairs with `luagen/` (Phase 1 = free fns).
Members/types with a `platforms` annotation are wrapped in preprocessor guards.

```bash
node luagen-types.js ../../../../docs/reference/reference-data.json \
                     ../../../../docs/reference/colors.json \
    > ../../src/generated/trussctype_generated.cpp
```

## Status — 110 types compile clean (isolation syntax-only witness), 0 errors

Generated from reference-data, verified by compiling against the real headers.
Derives everything from the data:
- **constructors** from the type's `constructors[]` (copy/move ctors dropped — non-copyable types)
- **operators** `+ - * / [] ==` (and `< <=`) → `sol::meta_function`; compound-assign / `=` / `<<` / `!=` skipped (no Lua equivalent)
- **properties** = fields, **methods** / **static methods** = member fns
- skips **protected/private** members (`access`), **template** members, **unbindable**
  args (out-param / pointer / array), **`internal::`** incomplete types, and **fields**
  whose `type` is internal::/pointer/array (reference-data now carries field `type`)
- **trailing-default arity expansion** (Lua optional args), like luagen Phase 1
- **reference returns** use `-> decltype(auto)` (chaining `*this`, non-copyable refs)
- qualifies TrussC types (`trussc::Vec2`), nested types (`trussc::SoundSource::Kind`),
  and falls back to owner-nested for unrecorded member typedefs (`Node::Ptr`)

### Excluded (6) — see Obsidian handoff
`Serial Node Thread SoundSource Environment VideoPlayerBase`: sol's new_usertype
instantiation pulls in the incomplete `internal::StreamInstance` through their C++
definition (NOT visible in reference-data signatures) — needs deeper handling / a
hand-written shim. `Json`/`Xml` keep custom Lua glue (hand-written). (PlayingSound
recovered once fields are filtered by `type`.)

## Integration — IN PRODUCTION
`setGeneratedTypeBindings()` (in `src/generated/trussctype_generated.cpp`) is called
from `setBindings()` and registers **99 generated usertypes**, including the core
value types (Vec2/Vec3/Vec4/IVec2/IVec3/Color/Font/Mesh/Path/PlayingSound/Quaternion/
Rect/Sound/SoundStream — their hand-written versions were removed). Still hand-written
in `tcxLua.cpp`: 13 types with custom members (raw-pointer marshalling, `end_*`
Lua-keyword renames, texture setters), enums/constants, and Json/Xml glue.
Excluded from generation: Thread/SoundSource/Environment/VideoPlayerBase (their C++
definition embeds the incomplete `internal::StreamInstance`, so `new_usertype<T>`
can't instantiate sol's traits) — plus Json/Xml (custom glue).

## Verify after regenerating
`cd ../../bindcheck && ./run_bindcheck.sh --regen` (existence + call asserts, incl.
regression checks for ctor default-arity and unary minus), then `./sweep_examples.sh`
for constructor/overload changes — a past ctor-arity bug passed the existence check
but crashed `Color.new(1,0,0)` at runtime. See `bindcheck/README.md`.
