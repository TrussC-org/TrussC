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
  - calls are qualified: `provider:"std"` symbols (sin/cos/min…) → `std::name`,
    everything else → `trussc::name` (unambiguous, no std clash)
  - signatures flagged `tmpl:true` (template-derived phantom overloads) are skipped

## Status
**In production** — ~420 free functions generated from reference-data.json, adopted
as the real bindings (`src/generated/trussc_generated.cpp`, called from
`setBindings()`). Skipped: type members (→ `tools/luagen-types`), sub-namespaced
fns, templates, unbindable args (out-params / raw pointers / C arrays), and
`hidden` symbols. Zero heuristics, zero denylist — every decision is read from
the data.

## Verify after regenerating
1. Compile witness: build any tcxLua app/example (compiling the generated file IS
   the structural check).
2. Runtime check: `cd ../../bindcheck && ./run_bindcheck.sh --regen` — asserts every
   binding is reachable from Lua and key calls work. See `bindcheck/README.md`.
