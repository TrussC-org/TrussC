#pragma once
// =============================================================================
// TC_* annotation macros — source-coupled metadata for the reference generator.
//
// These expand to `[[clang::annotate("tc:...")]]` under Clang, which the
// reference structure generator (docs/reference/structure.js) reads back from
// the AST to enrich a symbol with information that can't be derived from its
// signature: which platforms it exists on, and how a template should be
// instantiated for the Lua binding. (Hiding an internal-but-public symbol is
// done doc-side with `hide = true` in api-reference.toml — no source macro.)
//
// Under non-Clang compilers (MSVC, GCC) they expand to NOTHING — pure no-ops
// with zero runtime, ABI or codegen effect. The reference is generated with
// Clang, so the annotations are always available where they matter.
//
// Usage (place the macro immediately before the declaration):
//
//     TC_PLATFORMS("macos,windows,linux") void grabScreen();   // desktop only
//     TC_LUA_BIND("float,Vec2,Vec3,Color") class Tween;        // bind these T
//
// Note: the annotation STRING is not emitted into Clang's JSON AST, so the
// generator recovers it from the source via each AnnotateAttr's source range.
// Keep the macro on the same declaration it annotates.
// =============================================================================

#if defined(__clang__)
  #define TC_ANNOTATE(x) [[clang::annotate(x)]]
#else
  #define TC_ANNOTATE(x)
#endif

// Restrict a symbol to a comma-separated platform list in the reference.
// Platforms: macos, windows, linux, web, android, ios.
#define TC_PLATFORMS(list) TC_ANNOTATE("tc:platforms:" list)

// Lua: instantiate + bind this template at the given comma-separated types.
#define TC_LUA_BIND(types) TC_ANNOTATE("tc:lua_bind:" types)
