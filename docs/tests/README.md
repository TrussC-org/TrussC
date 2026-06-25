# `docs/tests/` — api-definition contract checks

These scripts verify that [`../api-definition.yaml`](../api-definition.yaml) — the
single source of truth for the documented API — stays in sync with the **real
C++ API** in `core/include/`. They are *checks*, not generators: nothing here
writes docs. (The doc generators live next door in [`../scripts/`](../scripts/).)

The contract is verified in **both directions**, with the compiler as the
oracle in each:

| Direction | Question | Tool | Status |
|-----------|----------|------|--------|
| **C++ → yaml** | Is every public symbol documented? | `coverage-audit.js` | ✅ |
| **yaml → C++** | Does every documented symbol exist with the documented signature? | `signature-probe.js` | ✅ |

Why two tools: neither half implies the other. The probe proves the yaml isn't
*lying* (no entry describes a signature the compiler rejects); coverage proves
the yaml isn't *incomplete* (no public symbol is silently undocumented).

## coverage-audit.js  (C++ → yaml)

Enumerates the real public surface from a `clang -ast-dump=json` of
`#include <TrussC.h>`, drops anything under an excluded namespace (declared in
`api-definition.yaml` → `coverage.excluded_namespaces`), then diffs what remains
against the documented symbols.

```sh
node docs/tests/coverage-audit.js              # human-readable report
node docs/tests/coverage-audit.js --json       # machine-readable residual
node docs/tests/coverage-audit.js --ast <file> # reuse a cached AST dump
node docs/tests/coverage-audit.js --save-ast <file>
```

- **Report-only.** It never fails a build. The residual mixes genuine doc gaps
  with intentional-but-public internals; bucketing it into *document / hidden /
  ignore* is the one-time closed-world triage, not a per-run gate.
- The full dump is ~280 MB; the script re-execs itself with a larger V8 heap.
- macOS/Metal compile flags are the default. On another host set
  `TC_COVERAGE_CXXFLAGS` to your own flags — the public surface is mostly
  platform-independent, but the dump must compile.

## signature-probe.js  (yaml → C++)

Generates a `.cpp` that `static_cast`s the address of each documented symbol to
its documented signature (`static_cast<Ret (Owner::*)(P) const>(&Owner::name)`),
so the **compiler** checks exact params / return / const-ness — no fragile
string comparison. A `-fsyntax-only` compile is enough for the type checks;
the existing example builds already cover linkage of out-of-line definitions.
Covers free functions, type methods, statics, and the `operators:` /
`free_operators:` schema (member + free, const-ness inferred per symbol). Every
failed cast is mapped back to its yaml entry, and the compiler's
`static_cast from 'REAL'` diagnostic gives the ground-truth signature to fix to.

```sh
node docs/tests/signature-probe.js            # human-readable mismatch report
node docs/tests/signature-probe.js --json      # machine-readable mismatch list
node docs/tests/signature-probe.js --strict    # exit non-zero on any mismatch (CI gate)
node docs/tests/signature-probe.js --keep       # keep the generated probe .cpp + map
```

- **Report-only by default.** The residual is a mix of genuine yaml signature
  lies (e.g. a chaining setter documented `void` that really returns `T&`) and
  probe-generator limits (templated params, probed protected hooks) that want a
  `hidden`/skip flag. Bucketing it is the same closed-world triage coverage uses.
  Turn on `--strict` only once the yaml is probe-clean.
- macOS/Metal flags are the default; on another host set `TC_PROBE_CXXFLAGS`
  (space-separated) and/or `$CXX`.

## Dependencies

Node deps (just `js-yaml`) are declared in [`../package.json`](../package.json)
and resolve from `docs/node_modules` (shared with `../scripts/`). Run
`npm install` in `docs/` once.
