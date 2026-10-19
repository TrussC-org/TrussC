#pragma once

// =============================================================================
// AllFeaturesExample — API coverage support
//
// The example is CI's compile-and-link canary. coverage_generated.cpp (from
// docs/reference/emit-coverage.js) references every documented public core API
// once; coverage_manual.cpp adds what the generator can't express (templates,
// the bundled addons). All of it sits behind af::never, a volatile flag that
// is always false: the code is compiled and linked on every platform but never
// runs, so arguments can be conjured with af::val<T>() — a reference that is
// never dereferenced — and calls with side effects (dialogs, recording, audio)
// are safe to reference.
// =============================================================================

#include <TrussC.h>

#include <type_traits>
#include <utility>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// One macro per platform, for the TC_PLATFORMS("...") guards in the coverage.
#if defined(__EMSCRIPTEN__)
#define AF_WEB 1
#elif defined(__ANDROID__)
#define AF_ANDROID 1
#elif defined(__APPLE__) && TARGET_OS_IOS
#define AF_IOS 1
#elif defined(__APPLE__)
#define AF_MACOS 1
#elif defined(_WIN32)
#define AF_WINDOWS 1
#else
#define AF_LINUX 1
#endif

namespace trussc {
namespace af {

// Always false (volatile: the compiler must assume it can change).
extern volatile bool never;
// Never dereferenced; only there to give af::val<T>() something to cast.
extern void* volatile sink;

// A T to pass as an argument, of exactly the declared type. Never evaluated
// at runtime (see af::never).
template <class T>
std::remove_reference_t<T>& val() {
    return *static_cast<std::remove_reference_t<T>*>(sink);
}

// Methods are referenced from a struct derived from the owner, so type names
// the owner declares (Node's Ptr, HitResult, ...) resolve inside the generated
// code without qualification. Never instantiated.
template <class Owner>
struct Scope : Owner {};

void coverGenerated();   // coverage_generated.cpp
void coverManual();      // coverage_manual.cpp

// Reference everything (never actually runs).
inline void coverAll() {
    if (never) {
        coverGenerated();
        coverManual();
    }
}

} // namespace af
} // namespace trussc
