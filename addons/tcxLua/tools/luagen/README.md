# luagen

Generate tcxLua (Sol2) Lua bindings from `docs/reference/reference-data.json`
(the canonical AST-derived artifact). **Fully data-driven — no heuristics, no
denylist, no npm deps.** Correctness is verified by COMPILING the output against
the real headers (witness).

## Run
```bash
node luagen.js ../../../../docs/reference/reference-data.json > ../../src/generated/trussc_generated.cpp
# reference-data.json is gitignored; regenerate it first:
#   node docs/reference/generate.js
```

## What it does (Phase 1 = free functions)
- Binds entries with `kind:"func"`, no `owner`, no `ns` (top-level `trussc::` free fns).
  Type members (`owner`) and sub-namespaced fns (`ns` → Lua tables) are Phase 2.
- Everything is derived from the structured `signatures[].args[]`
  (`{type, name, hasDefault, isRef, isConst, isPointer, isArray}`):
  - lambda decl + call come straight from `type`/`name` (synthesize `aN` if name empty)
  - **bindability** = skip a sig with a non-const lvalue-ref (out-param), raw pointer,
    or C-array arg; skip a fn with `tparams` (template)
  - `hasDefault` expands trailing defaults into arity overloads (Lua optional args)
  - reference returns (`ret` ends in `&`) use `-> decltype(auto)`
  - calls are qualified `trussc::name` (unambiguous — no std clash)

## Known reference-data.json gaps (surfaced by the witness — for the structure.js owner)
- **~19 std-math re-exports lack `args[]`** (`sin cos tan asin acos atan atan2 abs
  sqrt pow log exp min max floor ceil round fmod`): they're `using namespace std`
  visible, so structure.js sees no `ParmVarDecl`. They also aren't `trussc::`
  members, so even with args a `trussc::sin` call wouldn't resolve. These need
  to become real `trussc::` declarations (or be marked) to bind. **Essential for a
  sketch playground — worth fixing.**
- **`typeName`** has a phantom 0-arg overload (`args: []`) → `trussc::typeName()`
  doesn't exist. Likely a structure.js artifact.

Until those land, ~390 functions bind & compile clean; fixing them restores the
math functions and reaches full green with the generator unchanged.
