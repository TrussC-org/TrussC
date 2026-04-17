#pragma once

// =============================================================================
// tcHotReload.h — Hot reload support for TrussC apps
// =============================================================================
//
// Usage:
//
//   // tcApp.cpp — place TC_HOT_RELOAD at the top of your App implementation
//   #include "tcApp.h"
//   TC_HOT_RELOAD(tcApp)
//   void tcApp::setup() { ... }
//   void tcApp::draw() { ... }
//
//   // main.cpp — use TC_RUN_APP instead of runApp<>()
//   #include "tcApp.h"
//   int main() {
//       tc::WindowSettings settings;
//       return TC_RUN_APP(tcApp, settings);
//   }
//
// When TC_HOT_RELOAD is present in any src/*.cpp, CMake automatically splits
// the build into Host (EXE) + Guest (shared library). Editing and saving any
// source file in src/ triggers a rebuild of the Guest library and a live reload
// — no restart needed.
//
// To disable: comment out TC_HOT_RELOAD → next cmake configure reverts to
// static single-binary mode.
//
// Supported: macOS (.dylib), Linux (.so). Not supported: Windows, Wasm, iOS,
// Android (falls back to static mode).
// =============================================================================

// Note: this header is included BY TrussC.h (after all core types are defined),
// so it does NOT include TrussC.h itself to avoid circular dependency.

namespace trussc {
    // Forward declaration — implemented in tcHotReloadHost.h
    int runHotReloadApp(const WindowSettings& settings);
}

// ---------------------------------------------------------------------------
// TC_HOT_RELOAD(AppClass)
// Place in the .cpp file of your App subclass. Generates extern "C" factory
// functions that the Host uses to create/destroy your App via dlopen/dlsym.
// ---------------------------------------------------------------------------
#ifdef _WIN32
#define _TC_EXPORT extern "C" __declspec(dllexport)
#else
#define _TC_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define TC_HOT_RELOAD(AppClass)                                             \
    _TC_EXPORT trussc::App* tcHotReloadCreateApp() {                        \
        return new AppClass();                                               \
    }                                                                        \
    _TC_EXPORT void tcHotReloadDestroyApp(trussc::App* app) {               \
        delete app;                                                          \
    }

// ---------------------------------------------------------------------------
// TC_RUN_APP(AppClass, settings)
// Use in main.cpp instead of tc::runApp<AppClass>(settings).
// When compiled in hot reload mode (TC_HOT_RELOAD_BUILD defined by CMake),
// launches the hot reload host. Otherwise, falls through to normal static
// runApp.
// ---------------------------------------------------------------------------
#ifdef TC_HOT_RELOAD_BUILD
#define TC_RUN_APP(AppClass, settings)   \
    trussc::runHotReloadApp(settings)
#else
#define TC_RUN_APP(AppClass, settings)   \
    trussc::runApp<AppClass>(settings)
#endif
