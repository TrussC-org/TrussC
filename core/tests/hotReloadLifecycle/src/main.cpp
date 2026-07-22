// =============================================================================
// hotReloadLifecycle — regression test for the hot reload guest lifecycle
// =============================================================================
//
// Reproduces, without any window or runtime rebuild, the crash class where
// guest-library code leaks into host-owned state across reloads:
//
//   1. A function-local-static singleton first constructed by GUEST code
//      registers its exit destructor against the guest image's __dso_handle;
//      unloading an old guest then destroys an engine the host still uses
//      (macOS: recursive_mutex EINVAL on the next listen).
//   2. shared_ptr control blocks / std::function invokers allocated by an old
//      guest stay reachable from host state (e.g. Event<T>'s COW listener-list
//      snapshot) — unmapping the image turns their release into a jump into
//      unmapped memory (SEGV), or a crash in exit-time finalizers on Linux,
//      where dlclose often keeps images mapped and defers destructors to exit.
//
// The App base constructor/destructor traverses exactly that surface
// (AudioEngine listener auto-subscribe, Event COW list churn), so the test is
// just the real GuestLibrary driven through load -> create -> destroy ->
// unload cycles. The guest binary is identical each cycle — the bugs depend on
// image lifetime, not on the code changing — and GuestLibrary already loads
// each cycle from a fresh unique temp copy, exactly like a real reload.
//
// Exit code: 0 = survived all cycles (including process exit), non-zero or a
// crash = regression. TC_RUN_APP is intentionally NOT used: no sokol loop, no
// GPU, no file watcher, no cmake rebuild — CI-safe on every desktop platform.
// =============================================================================

#include "tcApp.h"

#ifndef TC_HOT_RELOAD_BUILD
// The cmake source scan keys the host/guest split off TC_HOT_RELOAD appearing
// in a .cpp: tcApp.cpp carries the real macro; this reference is inside an
// #ifndef so it never compiles, but keeps the intent greppable.
#error "hotReloadLifecycle must be configured as a hot reload (host/guest) build"
#endif

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Locate the guest library near the executable, mirroring Host::init()'s
// layout knowledge: exe = <proj>/bin/<name>[.app/Contents/MacOS/<name>],
// guest = <proj>/<buildDir>[/<config>]/libguest.{dylib,so}|guest.dll.
static std::string findGuestLibrary() {
#ifdef __APPLE__
    const char* guestName = "libguest.dylib";
#elif defined(_WIN32)
    const char* guestName = "guest.dll";
#else
    const char* guestName = "libguest.so";
#endif
    fs::path exe = fs::canonical(getExecutablePath());
    fs::path proj = exe.parent_path();
#ifdef __APPLE__
    for (int i = 0; i < 4 && proj.has_parent_path(); ++i) proj = proj.parent_path();
#else
    proj = proj.parent_path();
#endif
    const char* buildDirs[] = {"build-macos", "build-windows", "build-linux",
                               "build", "xcode", "vs"};
    const char* configs[] = {"", "Release", "Debug", "RelWithDebInfo"};
    for (const char* b : buildDirs) {
        for (const char* c : configs) {
            fs::path p = proj / b;
            if (*c) p /= c;
            p /= guestName;
            if (fs::exists(p)) return p.string();
        }
    }
    return "";
}

int main() {
    using namespace trussc::hot_reload;

    std::string guestPath = findGuestLibrary();
    if (guestPath.empty()) {
        std::printf("hotReloadLifecycle: FAIL - guest library not found\n");
        return 1;
    }
    std::printf("hotReloadLifecycle: guest = %s\n", guestPath.c_str());

    const int kCycles = 5;
    for (int i = 1; i <= kCycles; ++i) {
        GuestLibrary lib;
        if (!lib.load(guestPath)) {
            std::printf("hotReloadLifecycle: FAIL - load failed (cycle %d)\n", i);
            return 2;
        }
        // App construction walks the hazardous surface: first-touch of the
        // AudioEngine singleton, listener registration on host-owned Events.
        App* app = lib.create();
        if (!app) {
            std::printf("hotReloadLifecycle: FAIL - create failed (cycle %d)\n", i);
            return 3;
        }
        // Destruction + unload: listener removal churns the COW lists, and the
        // (pre-fix) dlclose here is what armed/triggered both crashes.
        lib.unload();
        std::printf("hotReloadLifecycle: cycle %d/%d ok\n", i, kCycles);
    }

    // Success is also surviving process exit: on Linux the pre-fix regression
    // fired in exit-time finalizers (SIGSEGV after main returned), which the
    // harness sees as a non-zero exit.
    std::printf("hotReloadLifecycle: OK\n");
    return 0;
}
