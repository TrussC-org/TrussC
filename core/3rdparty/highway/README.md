# Google Highway subset

Version: 1.2.0
Upstream commit: `457c891775a7397bdb0376bb1031e6e027af1c48` (release archive pax comment).
Tag: https://github.com/google/highway/tree/1.2.0
Archive: https://github.com/google/highway/archive/refs/tags/1.2.0.tar.gz
SHA-256: `7e0be78b8318e8bdbf6fa545d2ecb4c90f947df03f7aadc42c1967f019e63343`

Unmodified upstream files. No tests, contrib, build system or upstream docs.
`tcSimd.cpp` is the only TU including Highway; it includes the dispatch and
abort implementations directly so multi-target compilation is paid once.
Web and unsupported architectures use TrussC's scalar implementation.
PowerPC, RISC-V and WASM operation headers are omitted: the scalar switch
prevents Highway from being included on those architectures.

## Exact inventory

| File | Purpose |
|------|---------|
| `README.md` | TrussC subset provenance and inventory (this file) |
| `LICENSE` | Upstream Apache-2.0 license |
| `LICENSE-BSD3` | Upstream BSD-3-Clause license |
| `hwy/abort.cc` | Assertion/abort implementation required by dispatch and vector operations |
| `hwy/abort.h` | Transitive portable SIMD implementation dependency |
| `hwy/base.h` | Transitive portable SIMD implementation dependency |
| `hwy/detect_compiler_arch.h` | Transitive portable SIMD implementation dependency |
| `hwy/detect_targets.h` | Transitive portable SIMD implementation dependency |
| `hwy/foreach_target.h` | Re-includes tcSimd.cpp for each compiled target |
| `hwy/highway.h` | Portable SIMD operations and dynamic dispatch entry points |
| `hwy/highway_export.h` | Transitive portable SIMD implementation dependency |
| `hwy/ops/arm_neon-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/ops/arm_sve-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/ops/emu128-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/ops/generic_ops-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/ops/inside-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/ops/scalar-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/ops/set_macros-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/ops/shared-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/ops/x86_128-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/ops/x86_256-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/ops/x86_512-inl.h` | Target operation implementation transitively included by highway.h |
| `hwy/per_target.cc` | Per-target vector length detection needed by ARM runtime dispatch |
| `hwy/per_target.h` | Transitive portable SIMD implementation dependency |
| `hwy/targets.cc` | Runtime CPU detection and dispatch state |
| `hwy/targets.h` | Transitive portable SIMD implementation dependency |

## Updating

Verify the release hash before extraction, copy the include closure rooted at
`highway.h`, `foreach_target.h`, `targets.cc`, `abort.cc` and `per_target.cc`
for the enabled x86/ARM targets (including portable fallback operations), and
retain both licenses. Update this inventory and the Highway row in `docs/LICENSE.md`;
`tools/check_dependencies.py` checks version macros and provenance. Repeat
SIMD/scalar equality, platform compile checks and the compile-cost benchmark.
