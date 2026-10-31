#include <TrussC.h>
#include <cstdio>
#include <cstdlib>

extern "C" unsigned prewindowNativeQueries();

using namespace trussc;
namespace {
int failures = 0, checks = 0, constructors = 0;
void check(const char* phase, const char* name, bool ok) {
    ++checks;
    if (!ok) { ++failures; std::printf("pre-window: FAIL: %s: %s\n", phase, name); }
}
struct Snapshot {
    float dpi = getDpiScale();
    int width = getFramebufferWidth(), height = getFramebufferHeight();
};
// Exercise the first host-state access before main(), as a static Font can do.
const Snapshot initial;
void getters(const char* phase, float dpi, int width, int height) {
    check(phase, "capture window size", internal::captureWindowSize(internal::currentWindowContext()) == std::pair<size_t, size_t>(width, height));
    check(phase, "DPI", getDpiScale() == dpi);
    check(phase, "framebuffer width", getFramebufferWidth() == width);
    check(phase, "framebuffer height", getFramebufferHeight() == height);
    for (bool pixels : {false, true}) {
        internal::pixelPerfectMode() = pixels;
        const float scale = pixels ? 1.f : dpi;
        const int w = int(width / scale), h = int(height / scale);
        check(phase, "window width", getWindowWidth() == w);
        check(phase, "window height", getWindowHeight() == h);
        check(phase, "window size", getWindowSize() == Vec2(w, h));
        check(phase, "aspect ratio", getAspectRatio() == float(width) / height);
        auto& target = internal::currentTarget();
        const bool savedFbo = target.isFbo;
        const int savedWidth = target.width, savedHeight = target.height;
        for (bool fbo : {false, true}) {
            target.isFbo = fbo; target.width = 1001; target.height = 667;
            const float drawingScale = fbo ? 1.f : scale;
            check(phase, "drawing width", getWidth() == int(1001 / drawingScale));
            check(phase, "drawing height", getHeight() == int(667 / drawingScale));
            check(phase, "drawing size", getSize() == Vec2(int(1001 / drawingScale), int(667 / drawingScale)));
            check(phase, "target pixel scale", internal::targetPixelScale() == drawingScale);
            check(phase, "fractional target logical size", internal::targetLogicalSize() == Vec2(1001 / drawingScale, 667 / drawingScale));
        }
        target.isFbo = savedFbo; target.width = savedWidth; target.height = savedHeight;
        check(phase, "pixel to logical size", internal::framebufferToLogical(width, getDpiScale()) == int(width / dpi));
        check(phase, "App size scale", internal::appSizeDpi(getDpiScale()) == scale);
        check(phase, "pointer pixel to logical scale", internal::appPointerScale(getDpiScale()) == 1.f / scale);
    }
}
// Change native values before each reader, so another getter cannot refresh its
// cache on its behalf. Exercise drawing targets and both coordinate modes too.
void changingGetters(const char* phase) {
    struct Reader { const char* name; bool (*read)(float, int, int); };
    const Reader readers[] = {
        {"DPI independently", [](float d,int,int) { return getDpiScale()==d; }},
        {"framebuffer width independently", [](float,int w,int) { return getFramebufferWidth()==w; }},
        {"framebuffer height independently", [](float,int,int h) { return getFramebufferHeight()==h; }},
        {"capture independently", [](float,int w,int h) { return internal::captureWindowSize(internal::currentWindowContext())==std::pair<size_t,size_t>(w,h); }},
        {"window width independently", [](float d,int w,int) { return getWindowWidth()==int(w/(internal::pixelPerfectMode()?1.f:d)); }},
        {"window height independently", [](float d,int,int h) { return getWindowHeight()==int(h/(internal::pixelPerfectMode()?1.f:d)); }},
        {"window size independently", [](float d,int w,int h) { float s=internal::pixelPerfectMode()?1.f:d; return getWindowSize()==Vec2(int(w/s),int(h/s)); }},
        {"aspect independently", [](float,int w,int h) { return getAspectRatio()==float(w)/h; }},
        {"drawing width independently", [](float d,int,int) { float s=internal::currentTarget().isFbo||internal::pixelPerfectMode()?1.f:d; return getWidth()==int(1001/s); }},
        {"drawing height independently", [](float d,int,int) { float s=internal::currentTarget().isFbo||internal::pixelPerfectMode()?1.f:d; return getHeight()==int(667/s); }},
        {"drawing size independently", [](float d,int,int) { float s=internal::currentTarget().isFbo||internal::pixelPerfectMode()?1.f:d; return getSize()==Vec2(int(1001/s),int(667/s)); }},
        {"target scale independently", [](float d,int,int) { return internal::targetPixelScale()==(internal::currentTarget().isFbo||internal::pixelPerfectMode()?1.f:d); }},
        {"target extent independently", [](float d,int,int) { float s=internal::currentTarget().isFbo||internal::pixelPerfectMode()?1.f:d; return internal::targetLogicalSize()==Vec2(1001/s,667/s); }},
        {"conversion independently", [](float d,int w,int) { return internal::framebufferToLogical(w,getDpiScale())==int(w/d); }},
        {"App scale independently", [](float d,int,int) { return internal::appSizeDpi(getDpiScale())==(internal::pixelPerfectMode()?1.f:d); }},
        {"pointer scale independently", [](float d,int,int) { return internal::appPointerScale(getDpiScale())==(internal::pixelPerfectMode()?1.f:1.f/d); }},
    };
    const std::string savedDpi=std::getenv("TC_PREWINDOW_NATIVE_DPI");
    const std::string savedWidth=std::getenv("TC_PREWINDOW_NATIVE_WIDTH");
    const std::string savedHeight=std::getenv("TC_PREWINDOW_NATIVE_HEIGHT");
    auto& target=internal::currentTarget();
    const bool savedFbo=target.isFbo, savedPixels=internal::pixelPerfectMode();
    const int savedTargetWidth=target.width, savedTargetHeight=target.height;
    int index=0;
    for (bool pixels : {false,true}) for (bool fbo : {false,true}) {
        internal::pixelPerfectMode()=pixels;
        target.isFbo=fbo; target.width=1001; target.height=667;
        for (auto reader : readers) {
            const float dpi=(index%2)?1.5f:2.f;
            const int width=1501+index*2, height=901+index*2;
            setenv("TC_PREWINDOW_NATIVE_DPI", (index%2)?"1.5":"2", 1);
            setenv("TC_PREWINDOW_NATIVE_WIDTH", std::to_string(width).c_str(), 1);
            setenv("TC_PREWINDOW_NATIVE_HEIGHT", std::to_string(height).c_str(), 1);
            check(phase, reader.name, reader.read(dpi,width,height));
            ++index;
        }
    }
    target.isFbo=savedFbo; target.width=savedTargetWidth; target.height=savedTargetHeight;
    internal::pixelPerfectMode()=savedPixels;
    setenv("TC_PREWINDOW_NATIVE_DPI", savedDpi.c_str(), 1);
    setenv("TC_PREWINDOW_NATIVE_WIDTH", savedWidth.c_str(), 1);
    setenv("TC_PREWINDOW_NATIVE_HEIGHT", savedHeight.c_str(), 1);
}
struct ConstructorApp : App {
    ConstructorApp() {
        ++constructors;
        getters("App constructor", sapp_dpi_scale(), sapp_width(), sapp_height());
    }
};
struct HeadlessApp : App {
    HeadlessApp() {
        check("headless", "explicit non-window context", internal::currentWindowContext().hostWindow == nullptr);
        // Establish an earlier observation within this same headless run.
        (void)getDpiScale();
        setenv("TC_PREWINDOW_NATIVE_DPI", "1.25", 1);
        setenv("TC_PREWINDOW_NATIVE_WIDTH", "1701", 1);
        setenv("TC_PREWINDOW_NATIVE_HEIGHT", "951", 1);
        getters("headless constructor after native change", sapp_dpi_scale(), sapp_width(), sapp_height());
        changingGetters("headless constructor live readers");
    }
    void setup() override {
        setenv("TC_PREWINDOW_NATIVE_DPI", "2.5", 1);
        setenv("TC_PREWINDOW_NATIVE_WIDTH", "2701", 1);
        setenv("TC_PREWINDOW_NATIVE_HEIGHT", "1551", 1);
        getters("headless setup after another native change", sapp_dpi_scale(), sapp_width(), sapp_height());
        changingGetters("headless setup live readers");
        requestExit();
    }
};
}
int main() {
    getMainThreadId();
    check("static", "DPI", initial.dpi == sapp_dpi_scale());
    check("static", "width", initial.width == sapp_width());
    check("static", "height", initial.height == sapp_height());
    getters("prelaunch", sapp_dpi_scale(), sapp_width(), sapp_height());
    auto* main = internal::resolveWindow();
    check("prelaunch", "unbound", !main->isOpen());
    check("prelaunch", "closed Window width", main->getWidth() == 0);
    check("prelaunch", "closed Window height", main->getHeight() == 0);
    check("prelaunch", "closed Window visibility", !main->isOccluded());
    // Read-through must observe native changes after the very first access.
    setenv("TC_PREWINDOW_NATIVE_DPI", "1.5", 1);
    setenv("TC_PREWINDOW_NATIVE_WIDTH", "1501", 1);
    setenv("TC_PREWINDOW_NATIVE_HEIGHT", "901", 1);
    getters("live native before launch", sapp_dpi_scale(), sapp_width(), sapp_height());
    changingGetters("prelaunch live readers");
    auto secondary = std::make_shared<Window>();
    check("secondary", "unbound defaults", secondary->context().dpiScale == 1 && secondary->context().fbWidth == 0 && secondary->context().fbHeight == 0);
    WindowSettings settings;
    const sapp_desc descriptor = buildAppDescriptor<ConstructorApp>(settings);
    check("descriptor", "App factory is deferred until bootstrap", constructors == 0 && descriptor.init_cb != nullptr);
    check("descriptor", "unbound", !main->isOpen());
    getters("launch reset", sapp_dpi_scale(), sapp_width(), sapp_height());
    { auto app = internal::makeConstructedApp<ConstructorApp>(); }
    // Repeat the reset with different native values.
    setenv("TC_PREWINDOW_NATIVE_DPI", "2", 1);
    setenv("TC_PREWINDOW_NATIVE_WIDTH", "2561", 1);
    setenv("TC_PREWINDOW_NATIVE_HEIGHT", "1441", 1);
    internal::prepareLaunchWindow(settings);
    getters("second launch reset", sapp_dpi_scale(), sapp_width(), sapp_height());
    // Once bound, metadata remains authoritative despite different native values.
    sapp_tc_window_metadata metadata{};
    metadata.fb_width = 1801; metadata.fb_height = 1001; metadata.dpi_scale = 3;
    internal::publishWindowMetadata(*main, metadata);
    internal::WindowRequestAccess::bindForTests(*main, 1000);
    const unsigned boundQueries = prewindowNativeQueries();
    getters("main metadata", 3, 1801, 1001);
    check("main metadata", "bound readers make no native dimension queries", prewindowNativeQueries() == boundQueries);
    check("main metadata", "Window logical width", main->getWidth() == int(1801 / 3.f));
    check("main metadata", "Window logical height", main->getHeight() == int(1001 / 3.f));
    metadata.fb_width = 701; metadata.fb_height = 501; metadata.dpi_scale = 1.25f;
    internal::publishWindowMetadata(*secondary, metadata);
    internal::currentWindowCtx() = &secondary->context();
    getters("secondary metadata", 1.25f, 701, 501);
    internal::currentWindowCtx() = nullptr;
    getters("main isolated", 3, 1801, 1001);
    runHeadlessApp<HeadlessApp>();
    check("headless", "restores caller context", internal::currentWindowCtx() == nullptr);
    getters("headless leaves main intact", 3, 1801, 1001);
    // Headless reads through for its whole run without binding a native Window.
    setenv("TC_PREWINDOW_NATIVE_DPI", "1.5", 1);
    runHeadlessApp<HeadlessApp>();
    internal::WindowRequestAccess::unbind(*main);
    getters("main live again after unbind", sapp_dpi_scale(), sapp_width(), sapp_height());
    std::printf("pre-window: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
