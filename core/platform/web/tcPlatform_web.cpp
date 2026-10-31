// =============================================================================
// Emscripten/Web プラットフォーム固有機能
// =============================================================================

#ifdef __EMSCRIPTEN__

#include "TrussC.h"
#include <emscripten.h>
#include <map>
#include <emscripten/html5.h>
#ifdef __EMSCRIPTEN_PTHREADS__
#include <emscripten/threading.h>
#endif

namespace trussc {

float getDisplayScaleFactor() {
    // ブラウザの devicePixelRatio を取得
    return (float)emscripten_get_device_pixel_ratio();
}

// Immersive mode (no-op on web)
void setImmersiveMode(bool enabled) { (void)enabled; }
bool getImmersiveMode() { return false; }

// Orientation (no-op on web)
void setOrientation(Orientation mask) { (void)mask; }

namespace {
void wakeLockUnavailable() noexcept {
    // Log only after the browser-thread proxy returns; contain listener exceptions.
    try { logNotice("Platform") << "Screen Wake Lock unavailable; use a supported browser in a secure context (HTTPS or localhost)."; }
    catch (...) {}
}
EM_JS(int, applyWebWakeLock, (bool enabled), {
    let state = Module['trusscWakeLock'];
    if (!state) {
        if (!enabled) return 0;
        state = Module['trusscWakeLock'] = {
            requested: false, unsupported: false, notified: false, busy: false, sentinel: null, generation: 0
        };
        const visible = () => document.visibilityState === 'visible';
        state.pump = async () => {
            if (state.busy || state.unsupported) return;
            const generation = state.generation;
            state.busy = true;
            try {
                if (state.sentinel && state.sentinel.released) state.sentinel = null;
                if (state.sentinel && (!state.requested || !visible())) {
                    const sentinel = state.sentinel;
                    state.sentinel = null;
                    await sentinel.release();
                } else if (!state.sentinel && state.requested && visible()) {
                    const sentinel = await navigator.wakeLock.request('screen');
                    if (!state.requested || !visible() || generation !== state.generation) {
                        await sentinel.release();
                    } else {
                        state.sentinel = sentinel;
                        sentinel.addEventListener('release', () => {
                            if (state.sentinel === sentinel) state.sentinel = null;
                        }, { once: true });
                        if (sentinel.released) state.sentinel = null;
                    }
                }
            } catch (error) {
                // Hidden-page rejection is normal. Keep recovery armed, including for
                // visible-page rejections; retry on visibility change or disable/re-enable.
            } finally {
                state.busy = false;
                // Reconcile changes made while awaiting acquisition or release.
                if (generation !== state.generation) state.pump();
            }
        };
        state.changed = () => { ++state.generation; state.pump(); };
    }
    if (state.requested === !!enabled) return 0;
    state.requested = !!enabled;
    ++state.generation;
    if (enabled) {
        if (!globalThis.isSecureContext || !navigator.wakeLock) {
            state.unsupported = true;
            if (state.notified) return 0;
            state.notified = true;
            return 1;
        }
        state.unsupported = false;
        document.addEventListener('visibilitychange', state.changed);
    } else {
        document.removeEventListener('visibilitychange', state.changed);
    }
    state.pump();
    return 0;
});
int webWakeLockOnMain(int enabled) {
    return applyWebWakeLock(enabled != 0);
}
} // namespace
namespace internal {
void applyKeepScreenOn(bool enabled, bool headless) {
    (void)headless;
#ifdef __EMSCRIPTEN_PTHREADS__
    // The app main loop may itself be proxied to a pthread; DOM work stays in the browser.
    const int notice = emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_II, webWakeLockOnMain, int(enabled));
#else
    const int notice = webWakeLockOnMain(int(enabled));
#endif
    if (notice) wakeLockUnavailable();
}
} // namespace internal
#ifndef __EMSCRIPTEN_PTHREADS__
namespace { bool webKeepScreenRequested = false; }
void setKeepScreenOn(bool enabled) {
    webKeepScreenRequested = enabled;
    internal::applyKeepScreenOn(enabled, false);
}
bool getKeepScreenOn() { return webKeepScreenRequested; }
namespace internal {
void shutdownKeepScreenOn() noexcept {
    try { setKeepScreenOn(false); } catch (...) {}
}
} // namespace internal
#endif

IVec2 internal::singleWindowPosition() {
    logWarning("Platform") << "getWindowPosition() is not supported on Web";
    return IVec2(-1, -1);
}

void internal::setSingleWindowPosition(int x, int y) {
    logWarning("Platform") << "setWindowPosition() is not supported on Web";
    (void)x; (void)y;
}

void setWindowDecorated(bool decorated) {
    // No window decorations to toggle on Web.
    (void)decorated;
}

void internal::setSingleWindowSizeLogical(int width, int height) {
    // CSS layout owns the canvas. Changing only its drawable bypasses sokol's
    // WGPU attachment rebuild and stretches WebGL output at devicePixelRatio.
    (void)width; (void)height;
    logNotice("Platform") << "setWindowSize() is not supported on Web; canvas size follows page layout";
}

fs::path getExecutablePath() {
    return fs::path("/");
}

fs::path getExecutableDir() {
    return fs::path("/");
}

// User data / temp folders: the browser has no persistent file system here,
// so both live in Emscripten's in-memory file system and are gone on reload.
fs::path internal::platformUserDataRoot() {
    return fs::path("/userdata");
}

fs::path internal::platformTempRoot() {
    return fs::path("/tmp");
}

fs::path internal::platformAppBundlePath() {
    return {};
}

// ---------------------------------------------------------------------------
// Screenshot — deferred canvas download; synchronous Pixels stays unsupported.
// ---------------------------------------------------------------------------
static bool captureWindowWarned_ = false;

// Invoked by the afterFrame drain, before returning to the browser. toBlob()
// snapshots the rendered canvas now, even though its callback runs later, so
// neither WebGPU nor WebGL2 needs preserveDrawingBuffer.
using ScreenshotErrorCallback = void (*)(int);
EM_JS_DEPS(screenshotDownloadDeps, "$UTF8ToString,$getWasmTableEntry");
EM_JS(bool, downloadScreenshot, (const char* namePtr, const char* mimePtr,
                                 ScreenshotErrorCallback onError), {
    const name = UTF8ToString(namePtr);
    const mime = UTF8ToString(mimePtr);
    const fail = (reason) => getWasmTableEntry(onError)(reason);
    try {
        const canvas = Module['canvas'] || document.querySelector('#canvas');
        if (!canvas) { fail(0); return false; }
        canvas.toBlob((blob) => {
            if (!blob) { fail(2); return; }
            let url;
            let link;
            try {
                url = URL.createObjectURL(blob);
                link = document.createElement('a');
                link.href = url;
                link.download = name;
                document.body.appendChild(link);
                link.click();
            } catch (error) {
                fail(3);
            } finally {
                if (link) link.remove();
                // Keep the URL alive until the browser has consumed the click.
                if (url) setTimeout(() => URL.revokeObjectURL(url), 1000);
            }
        }, mime);
        return true;
    } catch (error) {
        fail(1);
        return false;
    }
});

static void screenshotDownloadError(int reason) {
    const char* message = reason == 0 ? "Screenshot canvas is unavailable" :
                          reason == 1 ? "Canvas screenshot failed (the canvas may be tainted by cross-origin content)" :
                          reason == 2 ? "Canvas screenshot encoding returned no image" :
                                        "Could not start the screenshot download";
    logError("Screenshot") << message;
}

std::pair<size_t, size_t> internal::captureWindowSize(const internal::WindowContext& ctx) {
    const auto dimensions = windowDimensions(ctx);
    return {size_t(std::max(0, dimensions.fbWidth)), size_t(std::max(0, dimensions.fbHeight))};
}

bool captureWindow(Pixels& outPixels) {
    (void)outPixels;
    if (!captureWindowWarned_) {
        captureWindowWarned_ = true;
        logWarning("Screenshot") << "grabScreen()/captureWindow() is not "
            "implemented on web (no canvas readback): no pixels are returned "
            "(returns false). Use the browser's own screenshot feature instead.";
    }
    return false;
}

bool internal::saveScreenshotPixels(const Pixels& pixels, const std::filesystem::path& path) {
    return writeScreenshotPixels(pixels, path).ok();
}

SaveResult internal::writeScreenshotPixels(const Pixels& pixels, const std::filesystem::path& path) {
    return pixels.save(internal::resolveScreenshotPath(path));
}

std::filesystem::path internal::resolveScreenshotDownloadName(const std::filesystem::path& path) {
    auto name = path.filename();
    if (name.empty()) name = "screenshot-" + getTimestampString() + ".png";
    const auto ext = toLower(getFileExtension(name));
    if (ext != "png" && ext != "jpg" && ext != "jpeg") {
        name += ".png";
        logWarning("Screenshot") << "Unsupported or missing extension; saving PNG to "
                                 << internal::pathToUtf8(name) << ". Supported formats: png, jpg/jpeg";
    }
    return name;
}

bool internal::captureWindowToFile(const std::filesystem::path& path) {
    const auto resolved = internal::resolveScreenshotDownloadName(path);
    const auto name = internal::pathToUtf8(resolved);
    const auto ext = toLower(getFileExtension(resolved));
    return downloadScreenshot(name.c_str(),
                              (ext == "jpg" || ext == "jpeg") ? "image/jpeg" : "image/png",
                              screenshotDownloadError);
}

namespace internal {
namespace {
unsigned nextScreenshotKey = 0;
auto& screenshotCallbacks() {
    static auto* callbacks = new std::map<unsigned, std::function<void(SaveResult)>>;
    return *callbacks;
}
using ScreenshotDone = void (*)(unsigned, int, const char*);
void screenshotDone(unsigned key, int success, const char* message) {
    auto node = screenshotCallbacks().extract(key);
    if (node.empty()) return;
    node.mapped()(success ? SaveResult::success() : SaveResult::fail(SaveError::WriteFailed, message));
}
EM_JS_DEPS(asyncScreenshotDeps, "$UTF8ToString,$getWasmTableEntry,$stringToNewUTF8");
EM_JS(void, asyncScreenshot, (unsigned key, const char* filename, ScreenshotDone done), {
    const name = UTF8ToString(filename);
    const queues = Module.trusscScreenshotQueues || (Module.trusscScreenshotQueues = new Map());
    let queue = queues.get(name);
    if (!queue) { queue = []; queues.set(name, queue); }
    const job = { key: key, ready: false, blob: null, error: "" };
    queue.push(job); // reserve order at capture, before toBlob callbacks can reorder
    const drain = () => {
        while (queue.length && queue[0].ready) {
            const head = queue.shift();
            let url, link, error = head.error;
            try {
                if (!error) {
                    url = URL.createObjectURL(head.blob);
                    link = document.createElement('a');
                    link.href = url; link.download = name;
                    document.body.appendChild(link); link.click();
                }
            } catch (caught) { error = caught.name + ': ' + caught.message; }
            finally {
                try { if (link) link.remove(); } catch (caught) { error = error || caught.message; }
                head.blob = null;
            }
            const notify = () => {
                const text = stringToNewUTF8(error);
                try { getWasmTableEntry(done)(head.key, error ? 0 : 1, text); }
                finally { _free(text); }
            };
            // Retain the C++ State/admission until download resources retire.
            if (url) setTimeout(() => {
                try { URL.revokeObjectURL(url); } finally { notify(); }
            }, 0);
            else notify();
        }
        if (!queue.length) queues.delete(name);
    };
    try {
        const canvas = Module['canvas'] || document.querySelector('#canvas');
        if (!canvas) throw new Error('Screenshot canvas is unavailable');
        canvas.toBlob(blob => {
            job.blob = blob;
            if (!blob) job.error = 'Canvas screenshot encoding returned no image';
            job.ready = true; drain();
        }, /[.]jpe?g$/i.test(name) ? 'image/jpeg' : 'image/png');
    } catch (error) {
        job.error = error.name + ': ' + error.message;
        job.ready = true; drain();
    }
});
}
void captureScreenshotDownload(const fs::path& path, std::function<void(SaveResult)> done) {
    unsigned key;
    do { key = ++nextScreenshotKey; } while (!key || screenshotCallbacks().contains(key));
    screenshotCallbacks().emplace(key, std::move(done));
    const auto name = pathToUtf8(path);
    asyncScreenshot(key, name.c_str(), screenshotDone);
}
}

// ---------------------------------------------------------------------------
// App menu (macOS only) — stub
// ---------------------------------------------------------------------------
namespace internal {
void installAppMenu() {}
} // namespace internal

// ---------------------------------------------------------------------------
// System sensors (stubs)
// ---------------------------------------------------------------------------
float getSystemVolume() { return -1.0f; }
void setSystemVolume(float volume) { (void)volume; }
float getSystemBrightness() { return -1.0f; }
void setSystemBrightness(float brightness) { (void)brightness; }
ThermalState getThermalState() { return ThermalState::Nominal; }
float getThermalTemperature() { return -1.0f; }
float getBatteryLevel() { return -1.0f; }
bool isBatteryCharging() { return false; }
Vec3 getAccelerometer() { return Vec3(0, 0, 0); }
Vec3 getGyroscope() { return Vec3(0, 0, 0); }
Quaternion getDeviceOrientation() { return Quaternion(1, 0, 0, 0); }
float getCompassHeading() { return 0.0f; }
bool isProximityClose() { return false; }
Location getLocation() { return Location(); }

void bringWindowToFront() {
    // no-op: web apps have no window management
}

} // namespace trussc

#endif // __EMSCRIPTEN__
