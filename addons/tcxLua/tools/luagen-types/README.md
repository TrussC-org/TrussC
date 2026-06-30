# luagen-types (Phase 2 — usertype generation)

Generates tcxLua Sol2 **usertype** bindings (constructors, properties, methods,
static methods, operators) from `docs/reference/reference-data.json`. Pairs with
`luagen/` (Phase 1 = free functions).

```bash
node luagen-types.js ../../../../docs/reference/reference-data.json > /tmp/gentypes.cpp
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

## Integration (v1) — WIRED IN
`setGeneratedTypeBindings()` (in `src/generated/trussctype_generated.cpp`) is called
from `setBindings()`. **v1 generates the 83 types that are NOT hand-written** (pure
coverage gain: EventArgs, color spaces, Tween<float/Vec2/Vec3>, network, etc.) — the
27 already-hand-written types stay hand-written for now (listed in EXCLUDE) to avoid
double-registration. **v2** will migrate the 14 hand types whose generated form is a
verified superset (Vec2/Vec3/Vec4/IVec2/IVec3/Color/Font/Mesh/Path/PlayingSound/
Quaternion/Rect/Sound/SoundStream) by deleting their hand usertypes.
