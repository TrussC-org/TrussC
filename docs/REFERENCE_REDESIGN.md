# API Reference System — Redesign

> Status: **design frozen** (pending validation by a prototype spike — see §11).
> Supersedes the hand-authored `api-definition.yaml` approach.

## 1. Why (the problem)

`api-definition.yaml` is a **hand-authored mirror of the C++ API**. A human re-types
every type / method / signature that already exists in the headers, and the
`signature-probe` + `coverage-audit` machinery exists only to keep that mirror in
sync. Mirroring by hand produced a class of defects that **CI could not see**:

- **125 duplicate `(owner, method)` entries** — whole types (Sound, AudioEngine,
  Color factories, Platform/GraphicsBackend/BuildInfo…) documented twice. The
  probe passes both copies; coverage credits a Set, so dups collapse silently.
- **Two representations for the same concept** — type methods live both as flat
  `self:`-bearing category entries (~1568) *and* under `types[].methods` (~674).
  The flat form renders **without its owning type** (`getBarWidth()` with the
  owner only hinted in a `(ScrollBar method)` description band-aid).
- **`cppName` / flat-name convention** — `name: Mat4_identity` + `cppName: identity`
  duplicates information and rendered as flat `Mat4_identity` or the double-prefix
  `Mat4::Mat4_identity`.
- **313 redundant double-signatures** — `a = 1.0f` and `a` listed as separate
  overloads of the same function.

Root cause: the schema lets a human express **structure**, so it can drift,
duplicate, and lose information. CI verifies *C++ correctness* (probe) and
*completeness* (coverage) — both stay green while the structure rots.

## 2. Core principle

**Derive everything derivable from the C++; hand-author only what is not.**

The C++ headers already *are* the definition of the API. Split the information:

| Half | Examples | Source |
|---|---|---|
| **Structure** | types, methods, params, return, static, const, owner, overloads, operators, public/internal, `[[deprecated]]` (+ reason) | **C++ AST** (derived) |
| **Prose / curation** | description (en/ja/ko), keywords, examples, category, oF mapping | **prose sidecar** (hand-authored) |
| **Source-coupled overrides** (low frequency) | `platforms`, template instantiation set, the rare "hidden but must stay in `trussc::`" | **`[[clang::annotate]]` in the source** (derived from the same AST pass) |

The bug class in §1 becomes **structurally impossible**: a human never writes
structure, so it cannot drift, duplicate, or lose its owner.

## 3. Source of truth, by field

| Field | From | Note |
|---|---|---|
| type / method / param / return / static / const / owner | AST | correct by construction; owner always present |
| overloads | AST | share one prose key (no per-overload prose) |
| operators | AST | structure + auto-templated description; prose optional |
| public vs internal | C++ namespace (§6) | `trussc::` direct = public |
| deprecated (+ reason) | `[[deprecated("…")]]` | already in the headers |
| **description / keywords / examples / category / oF** | **prose sidecar** | the only bulk hand-authoring |
| **platforms** | `[[clang::annotate]]` | AST-diff is unreliable (no-op stubs) → explicit |
| **template instantiation set** | `[[clang::annotate]]` | for binding codegen; author decides |
| **lua bindability** | AST signature (§8) | derived; no hand-authoring |

## 4. The prose sidecar

A **map keyed by symbol-id**, holding **prose only — no signatures**. Format:
**TOML** (chosen for: duplicate table/key = spec error → dup-definition is
*structurally impossible*; multi-line literals `'''…'''` ideal for descriptions
and code examples with no indentation sensitivity; quoted keys handle `::`).

Working file name: **`api-reference.toml`** (final name TBD — it is *reference
content / annotations*, **not** an API *definition*; the API is defined in C++).

### Entry shape (flat — no nesting beyond i18n)

```toml
["Color::fromHSB"]
category = "color"
keywords = ["hsv", "hue", "saturation", "brightness"]
of       = ["ofColor::fromHsb"]            # oF migration hint (field, not a key)
description.en = "Create a Color from HSB (all 0–1)."
description.ja = "HSB から Color を作成（すべて 0–1）。"
description.ko = "HSB로부터 Color 생성 (모두 0–1)."
example = '''
auto c = Color::fromHSB(0.5, 1.0, 1.0);
'''
```

- **One key per symbol; overloads share it** (signatures come from the AST) → the
  "313 redundant double-signatures" problem cannot occur.
- **No `signatures`, `self`, `static`, `return`, `cppName`** in the sidecar at all.

### Symbol-id grammar (the join key)

| Kind | Key | Example |
|---|---|---|
| method | `Type::method` | `Texture::getWidth` |
| free function | `funcName` | `drawRect` |
| namespaced free | `ns::funcName` | `bitmapfont::registerGlyphs` |
| type | `Type` | `Color` |
| template type | `Type<T>` (literal param as written) | `Tween<T>` |
| nested enum | `Type::EnumName` | `StrokeMesh::CapType` |
| constant | `CONST` / `ns::CONST` | `EventPriority::App` |
| operator | `Type::operator+` | (AST side; prose optional) |
| oF mapping | **a field (`of`), not a key** | — |

Dotted-key sub-table dups (`description.en` twice, or redefining a table) are
**also** TOML spec errors. Only `[[array.of.tables]]` is intentionally additive —
never use it for a symbol-id, only for genuinely repeatable lists (e.g. multiple
examples).

## 5. Source annotations (`[[clang::annotate]]`)

Low-frequency, source-coupled metadata lives **next to the definition** and is
read by the **same AST pass** that derives structure (the dump already carries
`AnnotateAttr`). Wrap in ergonomic, compiler-safe macros:

```cpp
#if defined(__clang__)
  #define TC_PLATFORMS(...)  [[clang::annotate("tc.platforms=" #__VA_ARGS__)]]
  #define TC_LUA_BIND(...)   [[clang::annotate("tc.lua_instantiate=" #__VA_ARGS__)]]
  #define TC_INTERNAL(why)   [[clang::annotate("tc.internal=" why)]]
#else
  #define TC_PLATFORMS(...)
  #define TC_LUA_BIND(...)
  #define TC_INTERNAL(why)
#endif

TC_PLATFORMS(linux, windows) void someDesktopOnlyThing();
template<class T> TC_LUA_BIND(float, Vec3, Color) class Tween { … };
TC_INTERNAL("ADL customization point; users call enumLabel()") /*…*/ tcEnumLabelsAdl(...);
```

`TC_INTERNAL` **replaces the entire `excluded_types` / `excluded_methods` /
`excluded_funcs` / `excluded_return_prefixes` config** — those hand-maintained
lists collapse into a reason-carrying annotation at each symbol's definition.

## 6. Visibility model

Visibility is **not** expressed in the sidecar. It is **C++-structural**
(matching the established principle: *anything directly in `trussc::` is the
public contract; non-public must live in a hidden namespace*):

- **Public** = in `trussc::` (not a hidden namespace) **and** not `TC_INTERNAL`.
- **Hidden** = in `internal::` / `mcp::` / `headless::` / `hot_reload::` /
  `console::` (primary mechanism), **or** `TC_INTERNAL("reason")` for the few
  symbols that must remain in `trussc::` (public bases like `HasTexture`, ADL
  hooks, `sg_*`-returning interop accessors).

Default is **expose**. A public symbol with no sidecar entry is still public —
it just lacks prose, surfaced by the `undocumented` check (§7).

## 7. Verification (replaces probe + coverage)

Because structure is no longer hand-authored, there is no signature to verify.
Three checks remain — all simple set / load operations:

| Check | Direction | Meaning | Severity |
|---|---|---|---|
| **dup-key** | within sidecar | same symbol-id / field defined twice | **hard error** (TOML spec + strict loader) |
| **orphan** | sidecar → AST | prose key has no matching real symbol (renamed/removed) | **hard error** (a doc that lies) |
| **undocumented** | AST → sidecar | public symbol has no prose | warn (or error once 100 % prose coverage is the goal) |

`orphan` is the spiritual successor to the probe: instead of checking that a
documented signature compiles, it checks that a documented **key resolves to a
real symbol**. The `signature-probe` is **retired** (the AST *is* the structure).

## 8. `lua` bindability (derived)

Rule: **bind everything bindable, no exceptions** (exposing a bindable symbol is
harmless). Bindability is a function of the AST signature, so it is **derived,
not hand-authored**:

- **Bindable** includes `shared_ptr<BoundType>` and `vector<shared_ptr<BoundType>>`
  — bound as the pointee handle. **This is what makes `Node` fully usable in Lua**
  (`getParent` / `getChildren` / `addChild` all bind). The old
  `shared_ptr → lua:false` heuristic was wrong.
- **Not bindable**: raw out-params (`float&`), raw pointers, templates (until
  instantiated via `TC_LUA_BIND`), raw arrays.

No `lua:` field in the sidecar.

## 9. oF mapping

A migration-guide convenience (look up a representative oF symbol → TrussC).
**Completeness is not a goal.**

- Stored as a per-symbol **`of` field** on the TrussC entry (keeps it from
  drifting away from the symbol). The reverse oF→TrussC index is **generated**.
- **oF symbols with no TrussC equivalent are simply omitted** (the old
  `ofOnlyEntries` list is dropped).
- Migrate the existing `of_equivalent:` values into the new `of` field.

## 10. Naming

The artifact no longer *defines* the API (C++ does); it **annotates / describes**
it. Rename away from `api-definition`. Candidates: `api-reference.toml`,
`reference-content.toml`, `doc-strings.toml`, `api-docs.toml`. (Decide during the
spike.)

## 11. Migration plan & open questions

**Plan:** (1) build the AST → structure generator (extend `coverage-audit`'s dump
to emit canonical symbol-ids + all structural fields). (2) **Mine prose** from the
current `api-definition.yaml` (the descriptions — especially ja/ko — are the
valuable asset) and re-key by symbol-id into `api-reference.toml`. (3) Build the
three checks (§7). (4) Add the `TC_*` macros and annotate the source. (5) Retire
the probe + exclusion configs.

**Validate first with a prototype spike (B)** over ~10 representative symbols
spanning every hard case, because the remaining unknowns are **empirical**, not
design:

- How clang emits a namespaced constant (`EventPriority::App`) in the AST.
- Whether same-name free functions collide across namespaces in the key scheme.
- Whether the chosen TOML library actually enforces dup-key / sub-table-dup.
- The hand-authoring ergonomics of the TOML entry shape (esp. examples + i18n).

Spike symbols: `Color::fromHSB` (overload), `Mat4::identity` (static), `drawRect`
(free), `bitmapfont::registerGlyphs` (ns free), `Tween<T>` (template),
`Vec2::operator+` (operator), `StrokeMesh::CapType` (nested enum),
`EventPriority::App` (ns const), `Node` (type), `Texture::getWidth` (plain method).

The spike may revise this document.
