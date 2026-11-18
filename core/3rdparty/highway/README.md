# Google Highway subset

Version: 1.4.0
Upstream commit: `2607d3b5b0113992fe84d3848859eae13b3b52c1` (release archive pax comment).
Tag: https://github.com/google/highway/tree/1.4.0
Archive: https://github.com/google/highway/archive/refs/tags/1.4.0.tar.gz
SHA-256: `e72241ac9524bb653ae52ced768b508045d4438726a303f10181a38f764a453c`

Unmodified upstream files. No tests, contrib, build system or upstream docs.
`tcSimd.cpp` is the only TU including Highway; it includes the dispatch and
abort implementations directly so multi-target compilation is paid once.
Web and unsupported architectures use TrussC's scalar implementation.
PowerPC, RISC-V, LoongArch and WASM operation headers are omitted:
the scalar switch prevents Highway from being included on those architectures.

## Exact inventory

| File | Purpose |
|------|---------|
| `README.md` | TrussC subset provenance and inventory (this file) |
| `LICENSE` | Upstream combined license file (Apache-2.0 and BSD-3-Clause for this subset) |
| `hwy/abort.cc` | Assertion/abort implementation required by dispatch and vector operations |
| `hwy/abort.h` | Compatibility header included by abort.cc |
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
| `hwy/ops/x86_avx3-inl.h` | AVX3 operation entry point transitively included by highway.h |
| `hwy/per_target.cc` | Per-target vector length detection needed by ARM runtime dispatch |
| `hwy/per_target.h` | Transitive portable SIMD implementation dependency |
| `hwy/targets.cc` | Runtime CPU detection and dispatch state |
| `hwy/targets.h` | Transitive portable SIMD implementation dependency |
| `hwy/x86_cpuid.h` | CPU feature detection transitively included by targets.cc |

## Updating

Verify the release hash before extraction, copy the include closure rooted at
`highway.h`, `foreach_target.h`, `targets.cc`, `abort.cc` and `per_target.cc`
for the enabled x86/ARM targets (including portable fallback operations), and
retain the combined upstream license file. Update this inventory and the Highway
row in `docs/LICENSE.md`; `tools/check_dependencies.py` checks version macros
and provenance. Repeat SIMD/scalar equality, platform compile checks and the
compile-cost benchmark.
