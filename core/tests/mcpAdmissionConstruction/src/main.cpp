#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>

// This test owns its binary: interposition must not affect other harnesses.
namespace {
bool tracking = false;
size_t largest = 0;
int checks = 0, failures = 0;
void check(const char* name, bool ok) {
    ++checks;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
}
void* operator new(std::size_t n) {
    if (tracking) largest = std::max(largest, n);
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t& tag) noexcept { return ::operator new(n, tag); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#ifdef TC_ADMISSION_CAPTURE_PROBE
namespace { int mainCaptureWidth = 32, mainCaptureHeight = 32; bool realCapture = false; }
extern "C" int __real_sapp_width();
extern "C" int __real_sapp_height();
extern "C" int __wrap_sapp_width() { return realCapture ? __real_sapp_width() : mainCaptureWidth; }
extern "C" int __wrap_sapp_height() { return realCapture ? __real_sapp_height() : mainCaptureHeight; }
#endif

using namespace trussc;
namespace {
alignas(16) unsigned char nativeStandIn[256]{};
struct OpenWindow : Window {
    OpenWindow() { native_ = nativeStandIn; }
    ~OpenWindow() { native_ = nullptr; }
};
std::string request(const char* name, json args = json::object()) {
    return json{{"jsonrpc", "2.0"}, {"id", 19}, {"method", "tools/call"},
        {"params", {{"name", name}, {"arguments", std::move(args)}}}}.dump();
}
bool toolError(const json& reply) {
    if (!reply.contains("result")) return false;
    const auto& result = reply.at("result");
    if (result.value("isError", false)) return true;
    if (result.at("content").at(0).value("type", "") == "image") return false;
    const auto text = json::parse(result.at("content").at(0).at("text").get<std::string>());
    return text.value("status", "") == "error";
}

int expectedDpi = 1;
bool gpuCompleted = false;
// Windows obtains high-DPI scale from the monitor; highDpi cannot force 2x.
bool skipMonitorDpi() {
#ifdef _WIN32
    if (expectedDpi == 2 && std::abs(getDpiScale() - 1.0f) < 0.01f) {
        printf("SKIP: DPI 2 needs a display scaled to 200%% (measured %.2f)\n", double(getDpiScale()));
        return true;
    }
#endif
    return false;
}
bool dpiSkipped = false;
void checkReadbackAdmission(size_t pixelCount, const void* target = nullptr) {
    using namespace mcp::internal;
    const auto defaults = mcp::getAdmissionLimits();
    const int window = target ? 1 : 0;
    const auto probePath = fs::temp_directory_path() / "tc-admission-rejection.png";
    for (const char* name : {"tc_save_screenshot", "tc_get_screenshot", "tc_analyze_image"}) {
        auto limits = defaults;
        // Readback pixels are independent of captureWindowSize. At DPI 2 a
        // logical-size admission fits this budget and must fail the checks.
        limits.workingBytes = std::string(name) == "tc_save_screenshot" ?
            pixelCount * 64 + 1024 * 1024 - 1 : pixelCount * 8 - 1;
        check("admission limits configuration succeeds", mcp::setAdmissionLimits(limits));
        const auto before = admissionRejections(AdmissionStore::Working);
        resetHttpChannel();
        auto promise = std::make_shared<std::promise<ReplyThunk>>();
        auto future = promise->get_future();
        json args = std::string(name) == "tc_analyze_image" ?
            json{{"source", {{"window", window}}}, {"ops", json::array({{{"op", "stats"}}})}} : json{{"window", window}};
        if (std::string(name) == "tc_save_screenshot") args["path"] = internal::pathToUtf8(probePath);
        // A thumbnail keeps later encoding within the budget even under the
        // logical-size mutation, so late encoder rejection cannot mask it.
        if (std::string(name) == "tc_get_screenshot") args["width"] = 16;
        getHttpChannel().send(mcp::McpRequest{request(name, args), promise, std::make_shared<WorkBudget>(true)});
        mcp::processHttpQueue();
        drainDeferredResponses(target);
        const bool ready = future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        bool rejected = false;
        if (ready) {
            try { rejected = toolError(json::parse(future.get()())); }
            catch (const AdmissionError&) { rejected = true; }
        }
        check("real framebuffer admission rejects screenshot tool", ready && rejected);
        check("real framebuffer rejection increments working counter once", admissionRejections(AdmissionStore::Working) == before + 1);
        check("real framebuffer rejection releases capture and credits", !hasDeferredResponses() && workingMemory() == 0);
        printf("real admission tool=%s working limit=%zu rejections=%llu\n", name, limits.workingBytes,
            static_cast<unsigned long long>(admissionRejections(AdmissionStore::Working) - before));
    }
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(defaults));
    std::error_code ec;
    fs::remove(probePath, ec);
}
// This mode deliberately uses native resizing and the normal frame prelude.
// No synthetic dimensions, drawable cleanup, or manual frame pumping here.
int resizeSaves = 0, resizeAccepted = 0, resizeCycles = 0;
int resizePointerChecks = 0, resizeWindows = 0, resizeSkipped = 0, resizeSkippedCycles = 0;
bool resizeCompleted = false;
class ResizeDisplayApp : public App {
    EventListener after;
    std::shared_ptr<Window> secondary;
    std::shared_ptr<ResizeDisplayApp> secondaryApp;
    std::future<mcp::internal::ReplyThunk> reply;
    fs::path output;
    static constexpr int resizeFrameLimit = 60;
    int windowIndex = 0, largeFrames = 0, resizeFrames = 0, cycles = 0;
    bool resizing = false;
    bool largeFrameSeen = false;
    bool waiting = false, finished = false, verifyAdmission = false;
    size_t savedPixelCount = 0;
    uint64_t rejections = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    internal::WindowContext& context() {
        return windowIndex ? getWindow()->context() : internal::mainWindowContext();
    }
    void checkCleared() {
        auto& ctx = context();
        ++resizePointerChecks;
        check("previous frame cleared both borrowed drawable pointers",
            !ctx.lastSwapchainDrawable && !ctx.lastSwapchain.metal.current_drawable);
    }
    void grow() {
        // Request 4K physical. Window managers may constrain this; record the
        // actual surface after three rendered frames, rather than assuming 4K.
        setWindowSize(3840 / expectedDpi, 2160 / expectedDpi);
        largeFrames = 0;
    }
    void skipResize(int requestedWidth, int requestedHeight) {
        const auto [w, h] = internal::captureWindowSize(context());
        printf("SKIP: window resize not supported here (requested %dx%d, measured %zux%zu)\n",
            requestedWidth, requestedHeight, w, h);
        printf("resize skipped window=%d remaining_cycles=%d\n", windowIndex, 3 - cycles);
        ++resizeSkipped;
        resizeSkippedCycles += 3 - cycles;
        resizing = false;
        finishWindow();
    }
    void enqueueInPrelude() {
        using namespace mcp::internal;
        check("large frame exceeds small framebuffer", largeFrameSeen);
        checkCleared();
        auto& ctx = context();
        const auto [w, h] = internal::captureWindowSize(ctx);
        printf("resize prelude window=%d cycle=%d current=%zux%zu previous=%dx%d budget=%zu\n",
            windowIndex, cycles + 1, w, h, ctx.lastSwapchain.width, ctx.lastSwapchain.height,
            mcp::getAdmissionLimits().workingBytes);
        check("next-frame prelude sees resized physical framebuffer",
            w == size_t(320 * expectedDpi) && h == size_t(240 * expectedDpi));
        rejections = admissionRejections(AdmissionStore::Working);
        auto promise = std::make_shared<std::promise<ReplyThunk>>();
        reply = promise->get_future();
        getHttpChannel().send(mcp::McpRequest{request("tc_save_screenshot",
            {{"path", internal::pathToUtf8(output)}, {"window", windowIndex}}),
            promise, std::make_shared<WorkBudget>(true)});
        // Same queue dispatcher as beginMainLoopFrame, while still in its
        // main-thread prelude, before beginFrame/update/draw on the main window.
        mcp::processHttpQueue();
        ++resizeSaves;
        const bool accepted = reply.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
            deferredResponses().size() == 1;
        if (accepted) ++resizeAccepted;
        check("real resized save accepted and deferred before drawing", accepted);
        check("enqueue has no stale-size working rejection",
            admissionRejections(AdmissionStore::Working) == rejections);
        check("deferred save retains bounded working credits",
            accepted && workingMemory() > 0 && workingMemory() <= mcp::getAdmissionLimits().workingBytes);
    }
    void afterFrame() {
        if (dpiSkipped) return;
        if (verifyAdmission) {
            verifyAdmission = false;
            checkReadbackAdmission(savedPixelCount, windowIndex ? &context() : nullptr);
            if (cycles == 3) finishWindow();
            else grow();
            return;
        }
        if (finished) return;
        if (resizing) {
            // afterFrame runs after this window's native tick has refreshed its
            // framebuffer cache. Only then enqueue for the next main prelude.
            const auto [w, h] = internal::captureWindowSize(context());
            if (w == size_t(320 * expectedDpi) && h == size_t(240 * expectedDpi)) {
                resizing = false;
                waiting = true;
                internal::mainThreadQueue().send([this] { enqueueInPrelude(); });
            } else if (++resizeFrames >= resizeFrameLimit) {
                skipResize(320 * expectedDpi, 240 * expectedDpi);
            }
            return;
        }
        if (waiting) return;
        if (++largeFrames < 3) return;
        const auto [w, h] = internal::captureWindowSize(context());
        largeFrameSeen = w > size_t(320 * expectedDpi) && h > size_t(240 * expectedDpi);
        if (!largeFrameSeen) {
            if (largeFrames >= resizeFrameLimit) skipResize(3840, 2160);
            return;
        }
        auto& ctx = context();
        printf("resize large window=%d cycle=%d actual=%dx%d logical=%dx%d dpi=%.2f\n",
            windowIndex, cycles + 1, ctx.lastSwapchain.width, ctx.lastSwapchain.height,
            getWindowWidth(), getWindowHeight(), double(getDpiScale()));
        setWindowSize(320, 240);
        resizing = true;
        resizeFrames = 0;
    }
    void finishWindow() {
        finished = true;
        if (windowIndex) return;
        if constexpr (internal::supportsSecondaryWindows()) {
            WindowSettings settings;
            settings.setSize(3840 / expectedDpi, 2160 / expectedDpi);
            settings.setHighDpi(expectedDpi == 2);
            settings.sampleCount = 1;
            secondary = createWindow(settings);
            check("supported platform creates real secondary window", bool(secondary));
            if (secondary) {
                secondaryApp = std::make_shared<ResizeDisplayApp>();
                secondaryApp->windowIndex = 1;
                secondary->setApp(secondaryApp);
                return;
            }
        } else {
            ++resizeSkipped;
            printf("SKIP: secondary windows unsupported on this platform\n");
        }
        resizeCompleted = true;
        exitApp();
    }
public:
    void setup() override {
        if (!windowIndex && skipMonitorDpi()) {
            dpiSkipped = true;
            exitApp();
            return;
        }
        ++resizeWindows;
        if (!windowIndex) {
            mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
            mcp::stopHttpServer();
            auto limits = mcp::getAdmissionLimits();
            // Independent requested logical dimensions times measured DPI. Leave
            // 3 MiB above the current physical save bound for request overhead;
            // the previous large framebuffer must still exceed this allowance.
            const auto scale = double(getDpiScale());
            limits.workingBytes = size_t(320 * scale) * size_t(240 * scale) * 64 + 4 * 1024 * 1024;
            limits.outstanding = 1; // Every completed save must return its slot.
            check("resize working budget configured", mcp::setAdmissionLimits(limits));
            mcp::internal::resetHttpChannel();
        }
        output = fs::temp_directory_path() / ("tc-admission-resize-" + std::to_string(windowIndex) + ".png");
        std::error_code ec;
        fs::remove(output, ec);
        after = events().afterFrame.listen([this] { afterFrame(); });
    }
    void update() override {
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(45)) {
            check("resize mode completes within 45 seconds", false);
            exitApp(1);
            return;
        }
        if (finished) {
            if (secondaryApp && secondaryApp->finished) {
                resizeCompleted = true;
                secondary->close();
                exitApp();
            }
            return;
        }
    }
    void draw() override {
        clear(0.2f, 0.4f, 0.6f);
        if (!waiting || !reply.valid()) return;
        // This closure runs after the current native callback has returned,
        // so both drawable pointers must already have been cleared by core.
        internal::mainThreadQueue().send([this] {
            using namespace mcp::internal;
            checkCleared();
            if (!reply.valid()) return;
            const bool ready = reply.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            check("real resized save completes after its frame", ready);
            if (!ready) return;
            bool ok = false;
            try { ok = !toolError(json::parse(reply.get()())); }
            catch (const std::exception& e) { printf("resize reply exception: %s\n", e.what()); }
            check("real resized save succeeds", ok);
            Pixels pixels;
            const bool loaded = bool(pixels.load(output));
            const auto [w, h] = internal::captureWindowSize(context());
            check("saved output equals current physical framebuffer",
                loaded && pixels.getWidth() == 320 * expectedDpi && pixels.getHeight() == 240 * expectedDpi &&
                size_t(pixels.getWidth()) == w && size_t(pixels.getHeight()) == h);
            check("completion has no stale-size working rejection",
                admissionRejections(AdmissionStore::Working) == rejections);
            check("completion releases deferred capture and all working credits",
                !hasDeferredResponses() && workingMemory() == 0);
            bool slotReleased = false;
            try {
                WorkScope scope(std::make_shared<WorkBudget>(true));
                auto credit = acquireScreenshotCredit();
                slotReleased = bool(credit);
            } catch (const AdmissionError&) {}
            check("completion returns the sole screenshot slot", slotReleased && workingMemory() == 0);
            printf("resize saved window=%d cycle=%d output=%dx%d credits=%zu\n",
                windowIndex, cycles + 1, pixels.getWidth(), pixels.getHeight(), workingMemory());
            std::error_code ec;
            fs::remove(output, ec);
            waiting = false;
            ++cycles;
            ++resizeCycles;
            // Independently predict the requested physical size even if saving failed.
            const auto scale = double(getDpiScale());
            savedPixelCount = size_t(320 * scale) * size_t(240 * scale);
            // Rejection probes need a live drawable; run in the next afterFrame.
            verifyAdmission = true;
        });
    }
    void cleanup() override {
        mcp::stopHttpServer();
        std::error_code ec;
        fs::remove(output, ec);
    }
};
class AdmissionDisplayApp : public App {
    EventListener after;
public:
    void setup() override {
        mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
        mcp::stopHttpServer();
        after = events().afterFrame.listen([] {
            if (gpuCompleted) return;
            gpuCompleted = true;
            if (skipMonitorDpi()) {
                dpiSkipped = true;
                exitApp();
                return;
            }
            using namespace mcp::internal;
            auto& ctx = internal::mainWindowContext();
            const auto [width, height] = internal::captureWindowSize(ctx);
            const int logicalWidth = getWindowWidth(), logicalHeight = getWindowHeight();
            Pixels pixels;
            check("real main swapchain readback succeeds", captureWindow(pixels));
            check("admission dimensions match real readback pixels",
                width == size_t(pixels.getWidth()) && height == size_t(pixels.getHeight()));
            check("real framebuffer has requested DPI ratio",
                logicalWidth > 0 && logicalHeight > 0 &&
                pixels.getWidth() == logicalWidth * expectedDpi && pixels.getHeight() == logicalHeight * expectedDpi);
            check("real main secondary-only dimensions remain zero", ctx.isMain && ctx.fbWidth == 0 && ctx.fbHeight == 0);
            printf("real screenshot DPI=%d: logical=%dx%d framebuffer=%zux%zu readback=%dx%d scale=%.2f\n",
                expectedDpi, logicalWidth, logicalHeight, width, height,
                pixels.getWidth(), pixels.getHeight(), double(getDpiScale()));
            checkReadbackAdmission(size_t(pixels.getWidth()) * size_t(pixels.getHeight()));
            exitApp();
        });
    }
    void draw() override { clear(0.2f, 0.4f, 0.6f); }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
#ifdef _WIN32
    _putenv_s("TRUSSC_MCP", "0");
#else
    setenv("TRUSSC_MCP", "0", 1);
#endif
    if (argc > 1) {
        if (argc != 3 || (std::strcmp(argv[1], "--gpu-check") != 0 && std::strcmp(argv[1], "--gpu-check-resize") != 0) ||
            (std::strcmp(argv[2], "--dpi=1") != 0 && std::strcmp(argv[2], "--dpi=2") != 0)) {
            printf("Usage: mcpAdmissionConstruction [--gpu-check|--gpu-check-resize --dpi=1|--dpi=2]\n");
            return 2;
        }
        expectedDpi = argv[2][6] - '0';
#ifdef TC_ADMISSION_CAPTURE_PROBE
        realCapture = true;
#endif
        WindowSettings settings;
        const bool resize = std::strcmp(argv[1], "--gpu-check-resize") == 0;
        settings.setSize(resize ? 3840 / expectedDpi : 320, resize ? 2160 / expectedDpi : 240);
        settings.setHighDpi(expectedDpi == 2);
        settings.sampleCount = 1;
        if (resize) return tcCoreTest::runDisplayTest<ResizeDisplayApp>(settings, failures, [] {
            if (dpiSkipped) {
                printf("mcpAdmissionConstruction RESIZE DPI=2: 0/0 checks, DPI-only run skipped\n");
                return;
            }
            check("real resize run completed", resizeCompleted);
            check("three resize cycles completed or skipped per window",
                resizeCycles + resizeSkippedCycles == resizeWindows * 3);
            printf("mcpAdmissionConstruction RESIZE DPI=%d: windows=%d skipped=%d cycles=%d saves=%d accepted=%d pointer_checks=%d checks=%d/%d failures=%d\n",
                expectedDpi, resizeWindows, resizeSkipped, resizeCycles, resizeSaves, resizeAccepted,
                resizePointerChecks, checks - failures, checks, failures);
        });
        return tcCoreTest::runDisplayTest<AdmissionDisplayApp>(settings, failures, [] {
            if (dpiSkipped) {
                printf("mcpAdmissionConstruction GPU DPI=2: 0/0 checks, DPI-only run skipped\n");
                return;
            }
            check("real framebuffer admission run completed", gpuCompleted);
            printf("mcpAdmissionConstruction GPU DPI=%d: %d/%d checks passed, %d failures\n",
                expectedDpi, checks - failures, checks, failures);
        });
    }
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    mcp::stopHttpServer();
    using namespace mcp::internal;
    const auto defaults = mcp::getAdmissionLimits();
    int workingWarnings = 0, connectionWarnings = 0, replyWarnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level != LogLevel::Warning) return;
        if (event.message.find("[MCP] working memory capacity ") == 0) ++workingWarnings;
        if (event.message.find("[MCP] connections capacity ") == 0) ++connectionWarnings;
        if (event.message.find("[MCP] reply capacity ") == 0) ++replyWarnings;
    });
    size_t mcp::AdmissionLimits::* const fields[] = {
        &mcp::AdmissionLimits::bodyBytes, &mcp::AdmissionLimits::jsonDepth,
        &mcp::AdmissionLimits::outstanding, &mcp::AdmissionLimits::workers,
        &mcp::AdmissionLimits::lineBytes, &mcp::AdmissionLimits::headerBytes,
        &mcp::AdmissionLimits::headerCount, &mcp::AdmissionLimits::histogramBins,
        &mcp::AdmissionLimits::gridCells, &mcp::AdmissionLimits::operations,
        &mcp::AdmissionLimits::replyBytes, &mcp::AdmissionLimits::workingBytes};
    auto sameLimits = [&](const mcp::AdmissionLimits& a, const mcp::AdmissionLimits& b) {
        for (auto field : fields) if (a.*field != b.*field) return false;
        return true;
    };
    int configurationWarnings = 0;
    auto configurationListener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Warning && event.message.find("[MCP] Admission limits rejected: ") == 0) {
            ++configurationWarnings;
            // Reentrant query verifies logging happens after unlocking.
            (void)mcp::getAdmissionLimits();
        }
    });
    auto expectConfigurationRejection = [&](const mcp::AdmissionLimits& limits) {
        const auto previous = mcp::getAdmissionLimits();
        const auto before = admissionRejections(AdmissionStore::Configuration);
        const int warningsBefore = configurationWarnings;
        check("configuration rejection returns false", !mcp::setAdmissionLimits(limits));
        check("configuration rejection logs exactly one Warning", configurationWarnings == warningsBefore + 1);
        check("configuration rejection increments counter once", admissionRejections(AdmissionStore::Configuration) == before + 1);
        check("configuration rejection preserves every limit", sameLimits(previous, mcp::getAdmissionLimits()));
    };
    auto configured = defaults;
    configured.bodyBytes /= 2;
    const auto beforeConfiguration = admissionRejections(AdmissionStore::Configuration);
    check("valid configuration returns true", mcp::setAdmissionLimits(configured));
    check("valid configuration applies every limit", sameLimits(configured, mcp::getAdmissionLimits()));
    check("valid configuration does not warn or count rejection", configurationWarnings == 0 &&
        admissionRejections(AdmissionStore::Configuration) == beforeConfiguration);
    for (auto field : fields) {
        auto invalid = defaults;
        invalid.*field = 0;
        expectConfigurationRejection(invalid);
    }
    {
        WorkScope scope;
        reserveWork(1);
        expectConfigurationRejection(defaults);
        expectConfigurationRejection(defaults);
    }
#ifndef __EMSCRIPTEN__
#ifdef _WIN32
    _putenv_s("TRUSSC_MCP", "1");
#else
    setenv("TRUSSC_MCP", "1", 1);
#endif
    mcp::startHttpServer();
    check("configuration test starts HTTP listener", mcp::getHttpPort() != 0);
    if (mcp::getHttpPort()) {
        expectConfigurationRejection(defaults);
        expectConfigurationRejection(defaults);
    }
    mcp::stopHttpServer();
#ifdef _WIN32
    _putenv_s("TRUSSC_MCP", "0");
#else
    setenv("TRUSSC_MCP", "0", 1);
#endif
#endif
    check("configuration rejection counter persists after HTTP stop",
        admissionRejections(AdmissionStore::Configuration) == beforeConfiguration + configurationWarnings);
    printf("configuration: %llu rejections, %d Warnings, previous limits retained\n",
        static_cast<unsigned long long>(admissionRejections(AdmissionStore::Configuration) - beforeConfiguration), configurationWarnings);
    configurationListener.disconnect();
    auto small = defaults;
    small.workingBytes = 1024 * 1024;
    small.bodyBytes = small.headerBytes = 1024;
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(small));
    {
        Image image;
        Fbo fbo;
        image.setDebugName(std::string(4 * 1024 * 1024, 'x'));
        fbo.setDebugName(std::string(4 * 1024 * 1024, 'y'));
        // The first rejection Warning synchronously reenters both registries.
        bool reentered = false;
        auto reentrant = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level != LogLevel::Warning || event.message.find("[MCP] working memory capacity ") != 0) return;
            image.setDebugName("warning observed");
            fbo.setDebugName("warning observed");
            Image created;
            Fbo createdFbo;
            created.setDebugName("created by listener");
            createdFbo.setDebugName("created by listener");
            reentered = !internal::debugObjects(internal::DebugObjectKind::Image).empty() &&
                !internal::debugObjects(internal::DebugObjectKind::Fbo).empty();
            (void)workingMemory();
        });
        const auto beforeWarning = admissionRejections(AdmissionStore::Working);
        check("reentrant registry Warning returns tool error", toolError(json::parse(Server::instance().processMessage(request("tc_list_images")))));
        check("Warning listener can rename, create, query and destroy Image/Fbo", reentered);
        check("reentrant Warning counts exactly one rejection", admissionRejections(AdmissionStore::Working) == beforeWarning + 1);
        check("reentrant Warning retires working credits", workingMemory() == 0);
        reentrant.disconnect();
        image.setDebugName(std::string(4 * 1024 * 1024, 'x'));
        fbo.setDebugName(std::string(4 * 1024 * 1024, 'y'));
        for (const auto& raw : {request("tc_list_images"), request("tc_list_fbos"),
                request("tc_analyze_image", {{"source", {{"image", 1}}}, {"ops", json::array({{{"op", "stats"}}})}}),
                request("tc_analyze_image", {{"source", {{"fbo", 1}}}, {"ops", json::array({{{"op", "stats"}}})}})}) {
            const auto before = admissionRejections(AdmissionStore::Working);
            largest = 0;
            std::string response;
            {
                WorkScope scope(std::make_shared<WorkBudget>(true));
                tracking = true;
                response = Server::instance().processMessage(raw);
                tracking = false;
            }
            const auto envelope = json::parse(response);
            check("oversized registry name returns a correlated tool error in HTTP scope", toolError(envelope) && envelope.at("id") == 19);
            check("snapshot rejects before any oversized allocation", largest <= small.workingBytes);
            check("snapshot rejection increments working counter", admissionRejections(AdmissionStore::Working) == before + 1);
            check("snapshot rejection retires credits", workingMemory() == 0);
            printf("registry probe: largest allocation=%zu, working limit=%zu\n", largest, small.workingBytes);
        }
    }
    check("registry overflow emits one working Warning", workingWarnings == 1);
    // Snapshot fits; incremental JSON storage does not fit the working budget.
    {
        Image image;
        image.setDebugName(std::string(200000, 'x'));
        const auto before = admissionRejections(AdmissionStore::Working);
        largest = 0; tracking = true;
        const auto response = Server::instance().processMessage(request("tc_list_images"));
        tracking = false;
        check("incremental JSON working admission returns tool error", toolError(json::parse(response)));
        check("incremental JSON rejects before oversized allocation", largest <= small.workingBytes);
        check("incremental JSON working rejection counted", admissionRejections(AdmissionStore::Working) == before + 1);
        check("incremental JSON working credits retire", workingMemory() == 0);
    }
    // Metadata is app-owned at registration; all request-time copies must be admitted.
    for (int field = 0; field < 9; ++field) {
        int owner;
        setRegistrationOwner(&owner);
        mcp::Tool tool;
        tool.name = "000_metadata_probe";
        tool.description = "small description";
        tool.handler = [](const json&) { return json(nullptr); };
        mcp::Resource resource;
        resource.uri = "000_probe://metadata";
        resource.name = "probe";
        resource.description = "small description";
        resource.mimeType = "text/plain";
        const std::string huge(4 * 1024 * 1024, 'x');
        if (field == 0) tool.description = huge;
        if (field == 1) tool.name = "000_" + huge;
        if (field >= 2 && field <= 4) {
            mcp::ToolArg arg{"probe", "string", "description", true};
            if (field == 2) arg.description = huge;
            if (field == 3) arg.name = huge;
            if (field == 4) arg.type = huge;
            tool.args.push_back(std::move(arg));
        }
        if (field == 5) resource.description = huge;
        if (field == 6) resource.uri = huge;
        if (field == 7) resource.name = huge;
        if (field == 8) resource.mimeType = huge;
        if (field < 5) Server::instance().registerTool(tool);
        else Server::instance().registerResource(resource);
        setRegistrationOwner(nullptr);
        const std::string raw = json{{"jsonrpc", "2.0"}, {"id", 19},
            {"method", field < 5 ? "tools/list" : "resources/list"}}.dump();
        const auto before = admissionRejections(AdmissionStore::Working);
        bool rejected = false;
        largest = 0;
        {
            WorkScope scope(std::make_shared<WorkBudget>(true));
            tracking = true;
            try { (void)Server::instance().processMessage(raw); }
            catch (const AdmissionError&) { rejected = true; }
            tracking = false;
        }
        check("oversized metadata/schema rejects in HTTP scope", rejected);
        check("metadata/schema rejects before oversized allocation", largest <= small.workingBytes);
        check("metadata/schema increments working counter", admissionRejections(AdmissionStore::Working) == before + 1);
        check("metadata/schema rejection retires credits", workingMemory() == 0);
        printf("metadata probe field=%d: largest allocation=%zu, working limit=%zu\n", field, largest, small.workingBytes);
        Server::instance().removeOwnedBy(&owner);
    }
    // Also exercise successful lists, including required/nullable schemas.
    {
        WorkScope scope;
        const auto tools = json::parse(Server::instance().processMessage(R"({"jsonrpc":"2.0","id":19,"method":"tools/list"})"));
        check("incrementally admitted tools list succeeds", tools.at("result").at("tools").is_array() && !tools.at("result").at("tools").empty());
        const auto resources = json::parse(Server::instance().processMessage(R"({"jsonrpc":"2.0","id":19,"method":"resources/list"})"));
        check("incrementally admitted resources list succeeds", resources.at("result").at("resources").is_array());
    }
    small = defaults;
    small.replyBytes = 4096;
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(small));
    {
        Image image;
        Fbo fbo;
        image.setDebugName(std::string(4096, 'x'));
        fbo.setDebugName(std::string(4096, 'y'));
        for (const char* tool : {"tc_list_images", "tc_list_fbos"}) {
            const auto before = admissionRejections(AdmissionStore::Reply);
            check("incremental JSON reply admission returns tool error", toolError(json::parse(Server::instance().processMessage(request(tool)))));
            check("incremental JSON reply rejection counted", admissionRejections(AdmissionStore::Reply) == before + 1);
            check("incremental JSON reply credits retire", workingMemory() == 0);
        }
    }
    check("reply overflow emits one Warning", replyWarnings == 1);
#ifdef TC_ADMISSION_CAPTURE_PROBE
    OpenWindow secondary;
    auto& main = internal::mainWindowContext();
    check("main window keeps secondary-only dimensions empty", main.isMain && main.fbWidth == 0 && main.fbHeight == 0);
    const std::initializer_list<int> windows{0, 1};
    auto dimensions = [&](int width, int height) {
        secondary.context().fbWidth = width;
        secondary.context().fbHeight = height;
        mainCaptureWidth = width;
        mainCaptureHeight = height;
    };
    small = defaults;
    small.outstanding = 1;
    small.workingBytes = 8 * 1024 * 1024;
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(small));
    dimensions(3840, 2160);
    for (int window : windows) {
        const auto before = admissionRejections(AdmissionStore::Working);
        largest = 0; tracking = true;
        const auto response = Server::instance().processMessage(request("tc_save_screenshot", {{"path", "admission.png"}, {"window", window}}));
        tracking = false;
        check("4K save rejects before capture in either window", toolError(json::parse(response)));
        check("4K save allocates no oversized readback or encoder buffer", largest <= small.workingBytes);
        check("4K save rejection counted and releases credits", admissionRejections(AdmissionStore::Working) == before + 1 && workingMemory() == 0);
    }
    auto queuedSave = [&](int window) {
        auto promise = std::make_shared<std::promise<ReplyThunk>>();
        auto future = promise->get_future();
        getHttpChannel().send(mcp::McpRequest{request("tc_save_screenshot", {{"path", "admission.png"}, {"window", window}}),
            promise, std::make_shared<WorkBudget>(true)});
        mcp::processHttpQueue();
        return future;
    };
    // Model a completed 4K frame, then resize before the next prelude. No
    // GPU is needed: the native dimension wrappers and real prelude dispatch
    // exercise enqueue independently of the previous frame's swapchain size.
    for (int window : windows) {
        auto& ctx = window ? secondary.context() : main;
        dimensions(3840, 2160);
        ctx.lastSwapchain.width = 3840;
        ctx.lastSwapchain.height = 2160;
        ctx.lastSwapchainDrawable = nativeStandIn;
        ctx.lastSwapchain.metal.current_drawable = nativeStandIn;
        ctx.clearFrameDrawable(); // same frame-end cleanup as Apple callbacks
        check("frame end clears both borrowed drawable pointers",
            !ctx.lastSwapchainDrawable && !ctx.lastSwapchain.metal.current_drawable);
        dimensions(320, 240);
        resetHttpChannel();
        auto promise = std::make_shared<std::promise<ReplyThunk>>();
        auto future = promise->get_future();
        getHttpChannel().send(mcp::McpRequest{request("tc_save_screenshot",
            {{"path", "admission.png"}, {"window", window}}), promise, std::make_shared<WorkBudget>(true)});
        const auto before = admissionRejections(AdmissionStore::Working);
        internal::beginMainLoopFrame(std::chrono::steady_clock::now());
        check("next-frame prelude admits resized 320x240 save after 4K frame",
            deferredResponses().size() == 1 && future.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
        check("next-frame enqueue charges current size without stale-size rejection",
            admissionRejections(AdmissionStore::Working) == before &&
            workingMemory() >= 5963776 && workingMemory() < small.workingBytes);
        mcp::stopHttpServer();
        check("cross-frame resize cleanup releases capture and credits",
            future.wait_for(std::chrono::seconds(0)) == std::future_status::ready &&
            toolError(json::parse(future.get()())) && !hasDeferredResponses() && workingMemory() == 0);
        printf("cross-frame resize window=%d: 3840x2160 -> 320x240, capture charge=5963776, limit=%zu\n",
            window, small.workingBytes);
    }
    for (int window : windows) {
        dimensions(32, 32);
        resetHttpChannel();
        const auto before = admissionRejections(AdmissionStore::Connections);
        auto first = queuedSave(window);
        int rejected = 0;
        for (int i = 1; i < 1000; ++i) {
            auto result = queuedSave(1 - window); // slot is shared across windows
            if (result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try { (void)result.get()(); } catch (const AdmissionError&) { ++rejected; }
            }
        }
        check("1000 saves retain only one deferred capture", deferredResponses().size() == 1 && rejected == 999);
        check("queued save retains working credits after dispatch", workingMemory() >= 1024 * 1024);
        check("queued MCP save does not enter application path queue", main.pendingScreenshotPaths.empty() && secondary.context().pendingScreenshotPaths.empty());
        check("999 excess saves increment rejection counter", admissionRejections(AdmissionStore::Connections) == before + 999);
        const auto& pending = deferredResponses().front();
        check("save defers to the requested window", pending.target == (window ? &secondary.context() : nullptr));
        // Grow after admission: drain must reject before native GPU readback.
        auto& ctx = window ? secondary.context() : main;
        dimensions(3840, 2160);
        const auto beforeGrowth = admissionRejections(AdmissionStore::Working);
        largest = 0; tracking = true;
        const void* ownerTarget = window ? &secondary.context() : nullptr;
        if (window) {
            auto* previous = internal::currentWindowCtx();
            internal::currentWindowCtx() = &ctx;
            drainDeferredResponses(ownerTarget);
            internal::currentWindowCtx() = previous;
        }
        else drainDeferredResponses();
        tracking = false;
        check("dimension growth rejects at drain before oversized allocation", largest <= small.workingBytes && toolError(json::parse(first.get()())));
        check("save growth rejection increments working counter", admissionRejections(AdmissionStore::Working) == beforeGrowth + 1);
        check("capture failure retires slot and working credits", !hasDeferredResponses() && workingMemory() == 0);
        printf("screenshot probe window=%d: 1000 requests, 1 queued, %d rejected\n", window, rejected);
    }
    // Readback-only tools defer admission until the actual capture point.
    for (int window : windows) {
        for (const char* name : {"tc_get_screenshot", "tc_analyze_image"}) {
            dimensions(32, 32);
            resetHttpChannel();
            auto promise = std::make_shared<std::promise<ReplyThunk>>();
            auto future = promise->get_future();
            json args = std::string(name) == "tc_get_screenshot" ? json{{"window", window}} :
                json{{"source", {{"window", window}}}, {"ops", json::array({{{"op", "stats"}}})}};
            getHttpChannel().send(mcp::McpRequest{request(name, args), promise, std::make_shared<WorkBudget>(true)});
            mcp::processHttpQueue();
            check("window readback waits for target frame", hasDeferredResponses() && future.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
            dimensions(3840, 2160);
            const auto before = admissionRejections(AdmissionStore::Working);
            largest = 0; tracking = true;
            auto* previous = internal::currentWindowCtx();
            if (window) internal::currentWindowCtx() = &secondary.context();
            drainDeferredResponses(window ? &secondary.context() : nullptr);
            internal::currentWindowCtx() = previous;
            tracking = false;
            check("grown window readback rejects before oversized allocation", largest <= small.workingBytes && toolError(json::parse(future.get()())));
            check("readback rejection counted and releases credits", admissionRejections(AdmissionStore::Working) == before + 1 && workingMemory() == 0);
        }
    }
    check("capture overflow emits one connections Warning", connectionWarnings == 1);
    // Shutdown also releases the deferred producer and its credits.
    dimensions(32, 32);
    resetHttpChannel();
    auto cancelled = queuedSave(0);
    mcp::stopHttpServer();
    check("shutdown answers admitted save and releases all credits", cancelled.wait_for(std::chrono::seconds(0)) == std::future_status::ready &&
        toolError(json::parse(cancelled.get()())) && workingMemory() == 0 && !hasDeferredResponses());

#endif
    check("admission limits configuration succeeds", mcp::setAdmissionLimits(defaults));
    listener.disconnect();
    printf("mcpAdmissionConstruction: %d/%d checks passed, %d failures\n", checks - failures, checks, failures);
    return failures ? 1 : 0;
}
