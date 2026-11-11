# luagen-types (Phase 2 — usertype generation)

Generates tcxLua Sol2 **usertype** bindings (constructors, properties, methods,
static methods, operators), **enums** (47, same usertype+`sol::var` style as the
old hand bindings), the **`colors` constant table** (from `colors.json`, 149
colors), and **45 global constants** — all from `docs/reference/`.
Pairs with `luagen/` (Phase 1 = free fns).
Members/types with a `platforms` annotation are wrapped in preprocessor guards.

Regenerate on macOS from the repository root:

```bash
docs/reference/regenerate.sh
```

See the [reference pipeline](../../../../docs/reference/README.md) for prerequisites,
all outputs and the CI freshness check.

## Status — 124 generated types

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

### Excluded and handwritten types

`Thread` and `Environment` are not bound. `SoundSource` has handwritten bindings.
These three are excluded from generation because of incomplete internal types.
`Serial`, `Node` and `VideoPlayerBase` are generated; the abstract
`VideoPlayerBase` has methods but no constructor. Namespaced APIs (`ns`) remain
excluded by both generators.

`Json`/`Xml` keep custom Lua glue. The other 13 handwritten custom types are
AudioEngine, EasyCam, Fbo, Image, Light, Mat3, Mat4, Material, MicInput, Pixels,
Shader, SoundBuffer and Texture. Their custom overloads and new members must be
maintained separately from generation.

`Tween` is excluded for a different reason: `defineTween` in `tcxLua.cpp` owns it and
registers the same C++ types under different Lua names (`TweenFloat` vs a generated
`Tween_float`). sol2 keeps **one metatable per C++ type**, so emitting both made one
registration silently lose its methods — and since both Lua names still resolved,
bindcheck's name-presence check stayed green while `tween:loop(-1)` crashed at
runtime. `TC_LUA_BIND` on `Tween<T>` stays: the two sketch-doc emitters read it and
document the `defineTween` surface. A **collision guard** now fails generation (exit
1, nothing written) if any C++ type is registered by both this generator and
`tcxLua.cpp` — including via a `defineXxx<T, …>(lua, "Name")` helper, whose runtime
name is invisible to literal-string greps.

## Integration — IN PRODUCTION
`setGeneratedTypeBindings()` (in `src/generated/trussctype_generated.cpp`) is called
from `setBindings()` and registers **124 generated usertypes**, **47 enums**,
**149 colors** and **45 global constants**. Counts describe generated output,
including platform-guarded registrations, rather than the entire Lua surface.
Custom types and Tween remain handwritten as described above.

## Verify after regenerating
`cd ../../bindcheck && ./run_bindcheck.sh --regen` (existence + call asserts, incl.
regression checks for ctor default-arity and unary minus), then `./sweep_examples.sh`
for constructor/overload changes — a past ctor-arity bug passed the existence check
but crashed `Color.new(1,0,0)` at runtime. See `bindcheck/README.md`.
