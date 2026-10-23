#pragma once
#include "tc/utils/tcSaveResult.h"
#include "tc/utils/tcResourceLoad.h"
#include <deque>

namespace trussc {
namespace fs = std::filesystem;
class Pixels;
struct ScreenshotSaveEventArgs {
    fs::path path;
    SaveResult result;
};
// Capture the requesting window at its next present; completion is delivered
// on main in a later frame. Native readback itself still runs on main.
// Completions are delivered during runApp / runHeadlessApp; in a bare main()
// without an app run, no completion event fires.
void saveScreenshotAsync(const fs::path& path);
// Completions are delivered during runApp / runHeadlessApp; in a bare main()
// without an app run, no completion event fires.
Event<ScreenshotSaveEventArgs>& onScreenshotSave();

namespace internal {
struct ScreenshotState;
struct ScreenshotQueue {
    ScreenshotQueue();
    ScreenshotQueue(const ScreenshotQueue&) = delete;
    ScreenshotQueue& operator=(const ScreenshotQueue&) = delete;
    std::shared_ptr<std::function<void()>> registration;
    std::shared_ptr<size_t> admitted = std::make_shared<size_t>(0);
    std::deque<std::shared_ptr<ScreenshotState>> pending;
    ~ScreenshotQueue();
};
void submitScreenshot(ScreenshotQueue* window, const fs::path& path);
void captureScreenshots(ScreenshotQueue& window);
void closeScreenshots(ScreenshotQueue& window) noexcept;
using ScreenshotCapture = std::function<bool(Pixels&)>;
using ScreenshotWriter = std::function<SaveResult(const Pixels&, const fs::path&)>;
// Main-thread test seams, changed only when workers have quiesced.
void setScreenshotBackendForTests(ScreenshotCapture capture, ScreenshotWriter writer);
#ifdef __EMSCRIPTEN__
void captureScreenshotDownload(const fs::path& path, std::function<void(SaveResult)> done);
#endif
}
}
