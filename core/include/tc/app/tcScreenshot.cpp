#include <TrussC.h>
#include <map>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#endif

namespace trussc {
Event<ScreenshotSaveEventArgs>& onScreenshotSave() {
    static auto* event = new Event<ScreenshotSaveEventArgs>;
    return *event;
}
namespace internal {
std::FILE* openScreenshotTemporary(const fs::path& path, fs::path& temporary) {
    static std::atomic<uint64_t> next{0};
    for (;;) {
        temporary = path;
        temporary += "." + std::to_string(next.fetch_add(1)) + ".tmp";
        // Exclusive creation also prevents collisions with other processes or
        // stale temporaries. Never truncate somebody else's in-flight encoder.
        auto* file = openFile(temporary, "wbx");
        if (file || errno != EEXIST) return file;
    }
}
struct ScreenshotState {
    fs::path path;
    fs::path destination;
    std::weak_ptr<std::atomic<bool>> run = appRunToken();
    std::shared_ptr<size_t> admitted;
    Pixels pixels;
    // A discarded completion must release its slot too (on main at app exit).
    ~ScreenshotState() { if (admitted) --*admitted; }
};
namespace {
struct ScreenshotTestBackend { ScreenshotCapture capture; ScreenshotWriter writer; };
ScreenshotTestBackend& testBackend() {
    static auto* backend = new ScreenshotTestBackend;
    return *backend;
}
struct Writers {
    std::mutex mutex;
    std::map<fs::path, std::deque<std::shared_ptr<ScreenshotState>>> paths;
};
Writers& writers() { static auto* value = new Writers; return *value; }
fs::path destinationKey(const fs::path& path) {
    // Rename replaces the final directory entry, including a symlink. Resolve
    // only its parent so our own first write cannot change the queue identity.
    auto key = fs::weakly_canonical(path.parent_path()) / path.filename();
#if defined(_WIN32) || defined(__APPLE__)
    auto parent = key.parent_path();
    while (!fs::exists(parent) && parent.has_relative_path()) parent = parent.parent_path();
#endif
#ifdef _WIN32
    // Query the directory, since Windows also supports case-sensitive names.
    const auto directory = CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    struct CaseSensitiveInfo { DWORD flags; } info{};
    // FileCaseSensitiveInfo (23); use the value for older SDK headers too.
    // Fold unless the directory positively reports case sensitivity: folding
    // a sensitive name only serializes two writers, never loses ordering.
    const bool sensitive = directory != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandleEx(directory, static_cast<FILE_INFO_BY_HANDLE_CLASS>(23),
            &info, sizeof(info)) && (info.flags & 1);
    const bool insensitive = !sensitive;
    if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
    if (!insensitive) return key;
    auto native = key.native();
    const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
        native.data(), static_cast<int>(native.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!size) throw std::system_error(GetLastError(), std::system_category());
    std::wstring folded(size, L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, native.data(),
        static_cast<int>(native.size()), folded.data(), size, nullptr, nullptr, 0))
        throw std::system_error(GetLastError(), std::system_category());
    key = folded;
#elif defined(__APPLE__)
    // Fold unless the volume positively reports case sensitivity (1).
    if (pathconf(parent.c_str(), _PC_CASE_SENSITIVE) != 1) {
        auto* text = CFStringCreateMutable(kCFAllocatorDefault, 0);
        CFStringAppendCString(text, key.c_str(), kCFStringEncodingUTF8);
        CFStringNormalize(text, kCFStringNormalizationFormD);
        CFStringFold(text, kCFCompareCaseInsensitive, nullptr);
        std::string folded(CFStringGetMaximumSizeForEncoding(CFStringGetLength(text),
            kCFStringEncodingUTF8) + 1, '\0');
        const bool ok = CFStringGetCString(text, folded.data(), folded.size(), kCFStringEncodingUTF8);
        CFRelease(text);
        if (!ok) throw std::runtime_error("Cannot normalize screenshot destination");
        folded.resize(std::strlen(folded.c_str()));
        key = folded;
    }
#endif
    return key;
}
void complete(const std::shared_ptr<ScreenshotState>& state, SaveResult result) {
    // Release the expensive payload before making its admission slot reusable.
    state->pixels = Pixels{};
    enqueueResourceLoad([state, result = std::move(result)]() mutable {
        if (state->admitted) { --*state->admitted; state->admitted.reset(); }
        if (!appRunIsAlive(state->run)) return;
        ScreenshotSaveEventArgs args{state->path, std::move(result)};
        onScreenshotSave().notify(args); // snapshot survives a listener deleting itself
    });
}
void write(const std::shared_ptr<ScreenshotState>& state) {
    auto& owner = writers();
    bool launch;
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        auto& queue = owner.paths[state->destination];
        launch = queue.empty();
        queue.push_back(state);
    }
    if (!launch) return;
    try {
        probeResourceLoad("screenshot-writer-launch", state->destination);
        startResourceLoad([path = state->destination, &owner] {
            for (;;) {
                std::shared_ptr<ScreenshotState> job;
                {
                    std::lock_guard<std::mutex> lock(owner.mutex);
                    job = owner.paths.at(path).front();
                }
                SaveResult result;
                try {
                    fs::path resolved;
                    result = prepareOutputPath(job->path, resolved, "Screenshot");
                    if (result) result = testBackend().writer ? testBackend().writer(job->pixels, job->path)
                                                   : writeScreenshotPixels(job->pixels, job->path);
                } catch (const std::exception& e) {
                    result = SaveResult::fail(SaveError::WriteFailed, e.what());
                } catch (...) {
                    result = SaveResult::fail(SaveError::WriteFailed, "Screenshot writer threw an unknown exception");
                }
                complete(job, std::move(result));
                std::lock_guard<std::mutex> lock(owner.mutex);
                auto& queue = owner.paths.at(path);
                queue.pop_front();
                if (queue.empty()) { owner.paths.erase(path); return; }
            }
        });
    } catch (...) {
        std::lock_guard<std::mutex> lock(owner.mutex);
        owner.paths.erase(state->destination);
        throw;
    }
}
}
ScreenshotQueue::ScreenshotQueue() = default;
ScreenshotQueue::~ScreenshotQueue() { registration.reset(); closeScreenshots(*this); }
void closeScreenshots(ScreenshotQueue& window) {
    auto jobs = std::move(window.pending);
    window.pending.clear();
    for (auto& job : jobs) complete(job, SaveResult::fail(SaveError::WriteFailed,
                                                       "Window closed before screenshot capture"));
}
void submitScreenshot(ScreenshotQueue* window, const fs::path& requested) {
    auto state = std::make_shared<ScreenshotState>();
#ifdef __EMSCRIPTEN__
    state->path = resolveScreenshotDownloadName(requested);
#else
    try { state->path = fs::absolute(resolveScreenshotPath(requested)).lexically_normal(); }
    catch (const std::exception& e) {
        state->path = requested;
        complete(state, SaveResult::fail(SaveError::WriteFailed, e.what()));
        return;
    }
#endif
    if (!window || *window->admitted >= 4
#ifndef __EMSCRIPTEN__
        || requested.empty()
#endif
        ) {
        complete(state, SaveResult::fail(SaveError::WriteFailed,
            !window ? "No render window for screenshot" : *window->admitted >= 4
                ? "Screenshot queue is full" : "No file name in path"));
        return;
    }
    // Merely creating a window must not start the async host/reaper.
    if (!window->registration)
        window->registration = registerResourceStop([window] {
            // std::exit may run on a worker. Its invalidated run silences
            // uncaptured jobs; window storage is touched only by main.
            if (isMainThread()) closeScreenshots(*window);
        });
    state->admitted = window->admitted;
    ++*state->admitted;
    window->pending.push_back(state);
}
void captureScreenshots(ScreenshotQueue& window) {
    auto jobs = std::move(window.pending);
    window.pending.clear();
    for (auto& state : jobs) {
        if (!appRunIsAlive(state->run)) { complete(state, SaveResult::fail(SaveError::WriteFailed, "App run ended")); continue; }
#ifdef __EMSCRIPTEN__
        captureScreenshotDownload(state->path, [state](SaveResult result) { complete(state, std::move(result)); });
#else
        try {
            if (!(testBackend().capture ? testBackend().capture(state->pixels) : captureWindow(state->pixels))) {
                complete(state, SaveResult::fail(SaveError::WriteFailed, "Screenshot capture failed"));
                continue;
            }
            state->destination = destinationKey(state->path);
            write(state);
        } catch (const std::exception& e) {
            complete(state, SaveResult::fail(SaveError::WriteFailed, e.what()));
        }
#endif
    }
}
void setScreenshotBackendForTests(ScreenshotCapture capture, ScreenshotWriter writer) {
    testBackend().capture = std::move(capture); testBackend().writer = std::move(writer);
}
}
void saveScreenshotAsync(const fs::path& path) {
    auto* window = !headless::active && sapp_isvalid() ? &internal::currentWindowContext() : nullptr;
    if (window && !window->isMain && !internal::currentWindow()) window = nullptr;
    internal::submitScreenshot(window ? &window->asyncScreenshots : nullptr, path);
    if (window && window->isMain) redraw();
}
}
