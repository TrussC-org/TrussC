#pragma once

// =============================================================================
// tcHotReloadHost.h — Hot reload host runtime
// =============================================================================
// This header is included only by the Host executable (when TC_HOT_RELOAD_BUILD
// is defined). It implements:
//   - Guest shared library loading via dlopen/dlsym
//   - Polling-based file watcher on src/
//   - Automatic Guest rebuild via cmake
//   - Live swap of the App instance without restarting the event loop
// =============================================================================

#ifdef TC_HOT_RELOAD_BUILD

// Note: this header is included BY TrussC.h (after all core types are defined).
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>

namespace trussc {
namespace hot_reload {

namespace fs = std::filesystem;
using std::string;
using std::vector;
using std::cout;
using std::cerr;
using Clock = std::chrono::steady_clock;

// Function pointer types for Guest exports
using CreateAppFn  = App* (*)();
using DestroyAppFn = void (*)(App*);

// ---------------------------------------------------------------------------
// Guest library manager
// ---------------------------------------------------------------------------

struct GuestLibrary {
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
    CreateAppFn createApp = nullptr;
    DestroyAppFn destroyApp = nullptr;
    App* app = nullptr;
    string loadedPath;  // actual path loaded (may be a temp copy on Windows)

    bool load(const string& path) {
#ifdef _WIN32
        // Windows: can't overwrite a loaded DLL, so copy to a temp name first
        static int loadCounter = 0;
        string tempPath = path + "." + std::to_string(loadCounter++) + ".tmp.dll";
        try { fs::copy_file(path, tempPath, fs::copy_options::overwrite_existing); }
        catch (...) { cerr << "[HotReload] Failed to copy DLL to " << tempPath << "\n"; return false; }

        handle = LoadLibraryA(tempPath.c_str());
        if (!handle) {
            cerr << "[HotReload] LoadLibrary failed (error " << GetLastError() << ")\n";
            return false;
        }
        createApp = (CreateAppFn)GetProcAddress(handle, "tcHotReloadCreateApp");
        destroyApp = (DestroyAppFn)GetProcAddress(handle, "tcHotReloadDestroyApp");
        if (!createApp || !destroyApp) {
            cerr << "[HotReload] GetProcAddress failed\n";
            FreeLibrary(handle);
            handle = nullptr;
            return false;
        }
        loadedPath = tempPath;
#else
        handle = dlopen(path.c_str(), RTLD_NOW);
        if (!handle) {
            cerr << "[HotReload] dlopen failed: " << dlerror() << "\n";
            return false;
        }
        createApp = (CreateAppFn)dlsym(handle, "tcHotReloadCreateApp");
        destroyApp = (DestroyAppFn)dlsym(handle, "tcHotReloadDestroyApp");
        if (!createApp || !destroyApp) {
            cerr << "[HotReload] dlsym failed: " << dlerror() << "\n";
            dlclose(handle);
            handle = nullptr;
            return false;
        }
        loadedPath = path;
#endif
        return true;
    }

    App* create() {
        if (createApp) {
            app = createApp();
            return app;
        }
        return nullptr;
    }

    void destroy() {
        if (app && destroyApp) {
            destroyApp(app);
            app = nullptr;
        }
    }

    void unload() {
        destroy();
        if (handle) {
#ifdef _WIN32
            FreeLibrary(handle);
            // Delete the temp copy
            try { fs::remove(loadedPath); } catch (...) {}
#else
            dlclose(handle);
#endif
            handle = nullptr;
        }
        createApp = nullptr;
        destroyApp = nullptr;
    }
};

// ---------------------------------------------------------------------------
// File watcher (polling-based, checks mtime of src/ files)
// ---------------------------------------------------------------------------

struct FileWatcher {
    vector<fs::path> watchPaths;
    fs::file_time_type lastBuildTime;

    void init(const string& srcDir) {
        watchPaths.clear();
        if (!fs::exists(srcDir)) return;
        for (const auto& entry : fs::recursive_directory_iterator(srcDir)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".mm") {
                    watchPaths.push_back(entry.path());
                }
            }
        }
        lastBuildTime = fs::file_time_type::clock::now();
    }

    bool hasChanges() {
        for (const auto& p : watchPaths) {
            try {
                if (fs::last_write_time(p) > lastBuildTime) return true;
            } catch (...) {}
        }
        return false;
    }

    void markBuilt() {
        lastBuildTime = fs::file_time_type::clock::now();
    }

    // Re-scan in case new files were added
    void rescan(const string& srcDir) {
        auto saved = lastBuildTime;
        init(srcDir);
        lastBuildTime = saved;
    }
};

// ---------------------------------------------------------------------------
// Find cmake binary (same logic as trusscli)
// ---------------------------------------------------------------------------

inline string findCMake() {
#ifdef __APPLE__
    const char* paths[] = {
        "/opt/homebrew/bin/cmake",
        "/usr/local/bin/cmake",
        "/Applications/CMake.app/Contents/bin/cmake",
    };
    for (const char* p : paths) {
        if (fs::exists(p)) return p;
    }
#endif
    return "cmake";
}

// ---------------------------------------------------------------------------
// Hot reload host — manages the Guest lifecycle
// ---------------------------------------------------------------------------

struct Host {
    GuestLibrary guest;
    FileWatcher watcher;
    string projectDir;   // project root (where CMakeLists.txt is)
    string buildDir;     // cmake build directory
    string guestLibPath; // path to the built guest library
    string srcDir;       // src/ directory to watch
    Clock::time_point lastCheck;
    int reloadCount = 0;
    bool initialized = false;

    bool init() {
        // Detect project directory from the executable's location
        // The Host EXE is at bin/<AppName>.app/Contents/MacOS/<AppName> (macOS)
        // or bin/<AppName> (Linux)
        fs::path exePath = fs::canonical(getExecutablePath());
        fs::path searchPath = exePath.parent_path();

        #ifdef __APPLE__
        // Climb out of MacOS → Contents → .app → bin → project root
        for (int i = 0; i < 4 && searchPath.has_parent_path(); ++i) {
            searchPath = searchPath.parent_path();
        }
        #else
        // bin → project root
        searchPath = searchPath.parent_path();
        #endif

        projectDir = searchPath.string();
        srcDir = projectDir + "/src";

        // Detect the build directory — try preset-style names first, fall back to build/
#ifdef __APPLE__
        const char* presetDirs[] = {"build-macos", "build"};
#elif defined(_WIN32)
        const char* presetDirs[] = {"build-windows", "build"};
#else
        const char* presetDirs[] = {"build-linux", "build"};
#endif
        buildDir = "";
        for (const char* d : presetDirs) {
            string candidate = projectDir + "/" + d;
            if (fs::exists(candidate + "/CMakeCache.txt")) {
                buildDir = candidate;
                break;
            }
        }
        if (buildDir.empty()) {
            // No configured build dir found — use the first preset name
            buildDir = projectDir + "/" + presetDirs[0];
        }

        if (!fs::exists(srcDir)) {
            cerr << "[HotReload] src/ directory not found at " << srcDir << "\n";
            return false;
        }

        // Guest library path
#ifdef __APPLE__
        guestLibPath = buildDir + "/libguest.dylib";
#elif defined(_WIN32)
        guestLibPath = buildDir + "/Release/guest.dll";
#else
        guestLibPath = buildDir + "/libguest.so";
#endif

        // Initial build of the Guest
        if (!rebuildGuest()) {
            cerr << "[HotReload] Initial Guest build failed\n";
            return false;
        }

        // Load the Guest and create the initial App instance
        if (!guest.load(guestLibPath)) {
            cerr << "[HotReload] Failed to load Guest library\n";
            return false;
        }
        if (!guest.create()) {
            cerr << "[HotReload] Failed to create initial App instance\n";
            return false;
        }

        // Start watching
        watcher.init(srcDir);
        lastCheck = Clock::now();
        initialized = true;

        logNotice("HotReload") << "Ready. Watching " << srcDir
                               << " (" << watcher.watchPaths.size() << " files)";
        return true;
    }

    bool rebuildGuest() {
        string cmake = findCMake();
        logNotice("HotReload") << "Rebuilding guest...";

        // Only rebuild the guest target — fast incremental build
        string cmd = cmake + " --build \"" + buildDir + "\" --target guest --parallel";
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            logError() << "[HotReload] Build failed (exit code " << rc << ")";
            return false;
        }
        return true;
    }

    bool reload() {
        // IMPORTANT: rebuild BEFORE unloading the old Guest. On macOS/Linux,
        // overwriting a loaded dylib is safe (the old inode stays in memory).
        // If the build fails, the old App keeps running undisturbed.

        if (!rebuildGuest()) {
            logWarning() << "[HotReload] Build failed — keeping current version";
            watcher.markBuilt();  // don't re-trigger on the same mtime
            return false;
        }

        // Build succeeded — now swap: destroy old App, unload old library,
        // load the new one.
        guest.unload();

        if (!guest.load(guestLibPath)) {
            logError() << "[HotReload] Failed to load rebuilt library";
            return false;
        }

        App* newApp = guest.create();
        if (!newApp) {
            logError() << "[HotReload] Failed to create App instance";
            return false;
        }

        reloadCount++;
        watcher.markBuilt();
        watcher.rescan(srcDir);

        logNotice("HotReload") << "Reloaded (#" << reloadCount << ")";
        return true;
    }

    // Called from the update loop — checks for file changes periodically
    void poll() {
        if (!initialized) return;

        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCheck);
        if (elapsed.count() < 500) return;  // Check every 500ms
        lastCheck = now;

        if (watcher.hasChanges()) {
            reload();
        }
    }

    App* getApp() { return guest.app; }
};

// ---------------------------------------------------------------------------
// Global host instance
// ---------------------------------------------------------------------------
inline Host g_host;

} // namespace hot_reload

// ---------------------------------------------------------------------------
// runHotReloadApp — entry point for hot reload mode
// ---------------------------------------------------------------------------

inline int runHotReloadApp(const WindowSettings& settings) {
    using namespace hot_reload;

    // Initialize the host (builds and loads Guest for the first time)
    // This must happen AFTER sokol is initialized, but we need the App*
    // before sapp_run. So we defer Guest loading to the setup callback.

    // Set up callbacks that go through the Host's dynamically loaded App
    internal::appSetupFunc = []() {
        if (!g_host.init()) {
            logError() << "[HotReload] Host initialization failed. Exiting.";
            sapp_quit();
            return;
        }
        // The Guest's App has been created by g_host.init() → guest.create().
        // TrussC's internal setup (screen size etc.) is already done by _setup_cb.
    };

    internal::appUpdateFunc = []() {
        // Poll for file changes
        g_host.poll();

        internal::updateFrameCount++;
        events().update.notify();
        App* app = g_host.getApp();
        if (app) {
            app->handleUpdate(internal::mouseX, internal::mouseY);
        }
    };

    internal::appDrawFunc = []() {
        events().draw.notify();
        App* app = g_host.getApp();
        if (app) app->handleDraw();
    };

    internal::appCleanupFunc = []() {
        App* app = g_host.getApp();
        if (app) {
            events().exit.notify();
            app->exit();
            app->cleanup();
        }
        g_host.guest.unload();
    };

    internal::appKeyPressedFunc = [](int key) {
        App* app = g_host.getApp();
        if (app) app->handleKeyPressed(key);
    };
    internal::appKeyReleasedFunc = [](int key) {
        App* app = g_host.getApp();
        if (app) app->handleKeyReleased(key);
    };
    internal::appMousePressedFunc = [](int x, int y, int button) {
        App* app = g_host.getApp();
        if (app) app->handleMousePressed(x, y, button);
    };
    internal::appMouseReleasedFunc = [](int x, int y, int button) {
        App* app = g_host.getApp();
        if (app) app->handleMouseReleased(x, y, button);
    };
    internal::appMouseMovedFunc = [](int x, int y) {
        App* app = g_host.getApp();
        if (app) app->handleMouseMoved(x, y);
    };
    internal::appMouseDraggedFunc = [](int x, int y, int button) {
        App* app = g_host.getApp();
        if (app) app->handleMouseDragged(x, y, button);
    };
    internal::appMouseScrolledFunc = [](float dx, float dy) {
        App* app = g_host.getApp();
        if (app) app->handleMouseScrolled(dx, dy,
            (int)internal::mouseX, (int)internal::mouseY);
    };
    internal::appWindowResizedFunc = [](int w, int h) {
        App* app = g_host.getApp();
        if (app) app->handleWindowResized(w, h);
    };
    internal::appFilesDroppedFunc = [](const std::vector<std::string>& files) {
        App* app = g_host.getApp();
        if (app) app->filesDropped(files);
    };

    // Build the sokol descriptor (without template — we handle App* manually)
    internal::pixelPerfectMode = settings.pixelPerfect;

    sapp_desc desc = {};
    if (settings.pixelPerfect) {
        float displayScale = getDisplayScaleFactor();
        desc.width = static_cast<int>(settings.width / displayScale);
        desc.height = static_cast<int>(settings.height / displayScale);
    } else {
        desc.width = settings.width;
        desc.height = settings.height;
    }
    desc.window_title = settings.title.c_str();
    desc.high_dpi = settings.highDpi;
    desc.sample_count = settings.sampleCount;
    desc.fullscreen = settings.fullscreen;
    desc.swap_interval = settings.swapInterval;
    desc.init_cb = internal::_setup_cb;
    desc.frame_cb = internal::_frame_cb;
    desc.cleanup_cb = internal::_cleanup_cb;
    desc.event_cb = internal::_event_cb;
    desc.logger.func = slog_func;
    desc.enable_dragndrop = true;
    desc.max_dropped_files = 16;
    desc.max_dropped_file_path_length = 2048;
    desc.enable_clipboard = true;
    desc.clipboard_size = settings.clipboardSize;
    internal::clipboardSize = settings.clipboardSize;

    sapp_run(&desc);
    return 0;
}

} // namespace trussc

#endif // TC_HOT_RELOAD_BUILD
