# bindgen (legacy)

This historical libclang generator is retained for reference. The current Lua
bindings come from the canonical `reference-data.json` through `luagen` and
`luagen-types`.

Regenerate all API outputs on macOS from the repository root:

```bash
docs/reference/regenerate.sh
```

See the [reference pipeline](../../../../docs/reference/README.md) for prerequisites,
outputs and the CI freshness check. Verify the bindings with the
[bindcheck harness](../../bindcheck/README.md).
