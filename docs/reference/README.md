# TrussC Reference Pipeline

The reference data (web reference, FOR_AI index, oF mapping, Lua bindings…) is
**derived**, not hand-maintained. Structure comes from the real C++ AST; prose
(descriptions, keywords, oF mapping) is hand-authored in a TOML sidecar; the two
are joined by **symbol-id** into one canonical artifact: `reference-data.json`.

This makes whole classes of rot *structurally impossible*: a symbol can't be
defined twice, a method can't lose its owner, a signature can't drift from the
source, because none of that is hand-typed — it's read from the compiler.

> Background / rationale: `../REFERENCE_REDESIGN.md`.

## Pipeline

```
core/include/**          (the real C++ source)
        │  clang -ast-dump=json
        ▼
   structure.js ──────────────►  structure  (symbol-id → { kind, signatures, … })
                                     │
api-reference.toml  ──────────►  prose      (symbol-id → { description, keywords, of })
   (hand-authored)                  │
        └──────────  generate.js  ──┴──►  reference-data.json   ← CANONICAL OUTPUT
                                          API_INDEX.md           (human/AI index)

   check.js   = verification gate (dup / orphan / undocumented)
```

| script         | role |
|----------------|------|
| `structure.js` | clang AST → structure (~2570 symbols). The id ORACLE. |
| `check.js`     | CI gate: `dup` (TOML parse), `orphan` (toml key ∉ AST), `undocumented` (AST ∉ toml; soft until `--strict`). |
| `generate.js`  | JOIN structure + prose by id → `reference-data.json` + `API_INDEX.md`. |

### Emitters (downstream — consume `reference-data.json`)

| script          | output |
|-----------------|--------|
| `emit-forai.js` | injects the C++ API index into `../FOR_AI_ASSISTANT.md` (documented-only, overloads collapsed, enum values, category-grouped). |
| `emit-of.js`    | the openFrameworks↔TrussC migration guide → `trussc.org/generated/of-mapping.json` + `../TrussC_vs_openFrameworks.md` §5. Grouping/notes from `of-mapping-config.js`. |
| `../scripts/emit-web.js` | the web reference data → `trussc.org/generated/trussc-api.js` (full public surface, same shape the site consumes). Reads `reference-data.json` + `extras.json` (macros / keywords / constant values / example links) + `colors.json`. |

The legacy `api-definition.yaml` and its `generate-docs.js` / `mine.js` are
**retired** (deleted). Structure comes from the AST, prose from
`api-reference.toml`, and the few things derivable from neither live in small
dedicated sources: `extras.json` (macros, keywords, constant values, manual
example links) and `colors.json` (the swatch palette, generated from
`tcColor.h` by `scripts/gen-colors.js`).

`api-reference.toml` is the **only** file humans edit. `reference-data.json` is
**auto-generated — never edit it by hand** (gitignored). Regenerate:

```bash
node --max-old-space-size=8192 generate.js          # runs clang, ~slow
node generate.js --structure /path/to/structure.json # reuse a cached structure
```

## `reference-data.json` — the canonical format

A flat object keyed by **symbol-id**. Downstream consumers (web reference,
FOR_AI_ASSISTANT.md, of-mapping, **Lua bindings**) read THIS file and nothing
else. Schema of one entry:

```jsonc
"Color::fromHSB": {
  "id":      "Color::fromHSB",   // canonical symbol-id (== the key)
  "kind":    "method",           // var | field | method | func | type | enum | typedef
  "name":    "fromHSB",          // bare display name
  "owner":   "Color",            // enclosing type   (method/field; absent for free fns)
  "ns":      "EventPriority",    // enclosing namespace/enum (enum members & ns'd symbols)
  "static":  true,               // static method (else absent)
  "tparams": ["T"],              // template params, literal names (templated types only)
  "signatures": [                // ONE entry per overload (callables); [] for type/enum
    { "ret": "Color", "params": "float h, float s, float b, float a = …", "const": false,
      "args": [                  // structured, parsed params (for binding generators)
        { "type": "float", "name": "h", "hasDefault": false },
        { "type": "float", "name": "a", "hasDefault": true }
      ] }
  ],
  "deprecated": { "reason": "…" },        // from C++ [[deprecated("…")]] — STRUCTURE side
  // ---- prose side (from api-reference.toml; absent when undocumented) ----
  "category":    "color",
  "keywords":    ["hsv","hue","saturation"],
  "of":          ["ofColor::fromHsb"],    // openFrameworks equivalents
  "of_category": "color-types",           // oF-mapping display group (types)
  "of_notes":    { "en": "…" },           // oF migration note (per oF mapping)
  "description": { "en": "…", "ja": "…", "ko": "…" },
  "details":     "longer prose…",
  "snippet":     "setColor(Color::fromHSB(0.5,1,1));",  // example code (authorable)
  "documented":  true                     // has description.en
}
```

### Symbol-id grammar (the join key)

| form                 | meaning                          | example                 |
|----------------------|----------------------------------|-------------------------|
| `freeFn`             | free function in `trussc::`       | `drawRect`              |
| `ns::fn`             | function in a sub-namespace       | `colors::lerp`          |
| `Type`               | class/struct/enum                 | `Color`, `Node`         |
| `Type<T>`            | class template (literal param)    | `Tween<T>`              |
| `Type::method`       | member function                   | `Color::fromHSB`        |
| `Type::field`        | data member                       | `Vec2::x`               |
| `Type::operator+`    | operator overload                 | `Vec2::operator+`       |
| `Enum::Member`       | enumerator (`kind:var`, `ns:Enum`)| `BlendMode::Add`        |
| `ns::CONST`          | namespaced constant/var           | `colors::cornflowerBlue`|

**Overloads share one id** — they appear as multiple entries in `signatures[]`.
This is the whole point: one key, one prose blurb, N signatures.

### Notes for consumers (incl. the Lua-binding script)

- **`params` is a display string** (named params; `= …` marks a defaulted param,
  the actual default expr is elided). For binding generators, each signature also
  carries a structured **`args[]`**: `{ type, name, hasDefault }` plus the flags
  `isRef` / `isConst` / `isPointer` / `isArray` (derived from the spelled C++
  type) when applicable — so you don't have to parse `params`. The default
  expression itself is still not reconstructed (`hasDefault` only).
- **Enum values** are NOT nested under the enum `type` entry. They are sibling
  `kind:"var"` entries with `ns` = the enum name (`BlendMode::Add`, ns `BlendMode`).
  Group by `ns` to reconstruct an enum's members.
- **`signatures: []`** for `type`/`enum`/`typedef` (non-callable).
- **Bindability** (Lua): derive from the signature. `shared_ptr<BoundType>` IS
  bindable. Unbindable: raw out-params, raw pointers, raw C arrays, and any
  signature with **`tmpl: true`** (a template overload needing explicit
  instantiation, e.g. `typeName<T>()` — skip those sigs). Visibility is
  namespace-driven — if it's in `reference-data.json` it's a public symbol.
- **`provider: "std"`** (sin/cos/lerp/…): these are NOT `trussc::` members —
  they're plain `std::` available to user code via `using namespace std`. Bind
  them via **`std::name`** (or unqualified), NOT `trussc::name`. They carry a
  full `args[]` like any other symbol.
- **`category`** is prose-supplied and may be absent; fall back to `owner`/`ns`.

## Visibility model (what gets into the reference at all)

Driven by C++ namespace, not by the sidecar:

- `trussc::` (direct) → **public**, included.
- `internal::`, `mcp::`, `headless::`, `hot_reload::`, `console::` → hidden, excluded.
- `private` members → excluded; `protected` → kept.

The sidecar never controls visibility — it only adds prose to symbols that are
already public.

### Source-coupled annotations (`TC_*`)

Some metadata can't be derived from a signature and is too source-local for a
sidecar. `core/include/tc/utils/tcAnnotations.h` provides macros that the
generator reads back from the AST:

| macro | effect |
|-------|--------|
| `TC_PLATFORMS("macos,windows,…")` | record the platforms a symbol exists on (`platforms` field) |
| `TC_LUA_BIND("float,Vec2,…")` | template instantiations to bind for Lua (`lua_bind` field) |

**Hiding a symbol** (public C++ but not user API — internal plumbing) is done
**doc-side**, not with a source macro: add `hide = true` to its `api-reference.toml`
entry. The symbol stays in the AST and `reference-data.json` (so CI validation and
the Lua binding generator still see it) but is excluded from every rendered surface
(web reference, FOR_AI index, oF guide). Prefer `internal::` namespace (free
functions) or `private`/`protected` (members) where they apply — `hide` is for the
residual "must be public but isn't API" cases (e.g. `App::handleDraw`, called by the
runtime).

They expand to `[[clang::annotate("tc:…")]]` under Clang and to nothing
otherwise (zero ABI/runtime effect). **Implementation note:** Clang's JSON AST
omits the annotate string, so `structure.js` recovers it from each
`AnnotateAttr`'s source range (`offset`, or `expansionLoc.offset` for the macro
form) — keep the macro on the same declaration it annotates.

## Status

`reference-data.json`: 2548 symbols, 1991 documented (78%). The remaining ~527
undocumented symbols just need prose added to `api-reference.toml` (their
structure is already correct). `check.js` passes (orphan 0, dup OK).
