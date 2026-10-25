// #847: exercise actual EGL surfaces, presents and caller restoration on X11.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__linux__) && !defined(__ANDROID__) && defined(SOKOL_GLES3)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
// Private native test entry, compiled only into the combined test backend.
namespace trussc::internal { void tickX11WindowForTests(sapp_window window); }
#endif

namespace {
#if defined(__linux__) && !defined(__ANDROID__) && defined(SOKOL_GLES3)
// A persistent three-window device probe using the production Window/App path.
// Automated --display below separately controls the native caller/present seams.
class InteractiveApp : public tc::App {
    int index_;
    tc::IVec2 position_;
public:
    explicit InteractiveApp(int index = 0) : index_(index), position_(20 + index * 320, 100) {}
    void setup() override { getWindow()->setPosition(position_.x, position_.y); }
    void draw() override {
        tc::clear(index_ == 1 ? .65f : .05f, index_ == 2 ? .65f : .05f, index_ == 0 ? .65f : .05f);
        tc::setColor(1);
        tc::drawBitmapString("EGL window " + std::to_string(index_) + " update=" + std::to_string(tc::getUpdateCount()) +
            " draw=" + std::to_string(tc::getDrawCount()), 10, 24);
        tc::drawBitmapString("F fullscreen | S capture | C close", 10, 48);
        tc::drawBitmapString("arrows move | V/B odd/default size", 10, 68);
        tc::drawBitmapString("1/2/3 fps 15/30/vsync | I independent", 10, 88);
        tc::drawBitmapString("E event-driven | R redraw(3)", 10, 108);
        tc::drawBitmapString("update fps=" + std::to_string(tc::getFrameRate()) +
            " dt=" + std::to_string(tc::getDeltaTime()), 10, 128);
        tc::drawCircle(tc::getMouseX(), tc::getMouseY(), 8);
    }
    void keyPressed(int key) override {
        if (key == 'F') tc::toggleFullscreen();
        else if (key == 'S') tc::saveScreenshot(tc::fs::absolute(".verification/pi5/egl-window-" + std::to_string(index_) + ".png"));
        else if (key == 'C') getWindow()->close();
        else if (key == 'V') tc::setWindowSizeLogical(333, 257);
        else if (key == 'B') tc::setWindowSizeLogical(280, 220);
        else if (key == '1') tc::setFps(15);
        else if (key == '2') tc::setFps(30);
        else if (key == '3') tc::setFps(tc::VSYNC);
        else if (key == 'I') tc::setIndependentFps(30, 15);
        else if (key == 'E') tc::setIndependentFps(tc::EVENT_DRIVEN, tc::EVENT_DRIVEN);
        else if (key == 'R') tc::redraw(3);
        else {
            if (key == tc::KEY_LEFT) position_.x -= 40;
            else if (key == tc::KEY_RIGHT) position_.x += 40;
            else if (key == tc::KEY_UP) position_.y -= 40;
            else if (key == tc::KEY_DOWN) position_.y += 40;
            else return;
            getWindow()->setPosition(position_.x, position_.y);
        }
    }
};
class InteractiveMain : public InteractiveApp {
    std::shared_ptr<tc::Window> secondary_[2];
public:
    void setup() override {
        tc::fs::create_directories(".verification/pi5");
        InteractiveApp::setup();
        for (int i = 0; i < 2; ++i) {
            tc::WindowSettings settings;
            settings.setSize(280, 220).setTitle(i == 0 ? "EGL Pi red" : "EGL Pi green");
            secondary_[i] = tc::createWindow(settings);
            if (secondary_[i]) secondary_[i]->setApp(std::make_shared<InteractiveApp>(i + 1));
            else tc::exitApp(1);
        }
    }
};

int failures = 0;
int phase = 0;
sapp_window redWindow{}, greenWindow{};
bool closeCaller = false;
EGLContext sharedContext = EGL_NO_CONTEXT;
EGLSurface mainSurface = EGL_NO_SURFACE;
int redDraws = 0, greenDraws = 0;
int redKeys = 0, greenKeys = 0, greenCloses = 0, redResizes = 0;
size_t liveAllocations = 0;
std::string capturePaths[3];
void* allocate(size_t bytes, void*) noexcept {
    void* memory = std::malloc(bytes);
    if (memory) ++liveAllocations;
    return memory;
}
void release(void* memory, void*) noexcept {
    if (memory) --liveAllocations;
    std::free(memory);
}

void check(const char* label, bool ok) noexcept {
    std::printf("eglWindowSurfaces %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

void clearColor(float r, float g, float b) noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
}

// Capture the X11 front image after swap, rather than the GL back buffer
// before it. A swap accidentally sent to the main surface must fail this.
void captureFront(const void* native, const char* path, int channel) noexcept {
    auto* display = (Display*)sapp_x11_get_display();
    const ::Window window = (::Window)(uintptr_t)native;
    XSync(display, False);
    XImage* image = XGetImage(display, window, 0, 0, 64, 64, AllPlanes, ZPixmap);
    check("front image acquired", image != nullptr);
    if (!image) return;
    const unsigned long masks[] = {image->red_mask, image->green_mask, image->blue_mask};
    const unsigned long pixel = XGetPixel(image, 32, 32);
    check(path, (pixel & masks[channel]) == masks[channel] &&
        (pixel & masks[(channel + 1) % 3]) == 0 && (pixel & masks[(channel + 2) % 3]) == 0);
    FILE* file = std::fopen(path, "wb");
    check("capture file opened", file != nullptr);
    if (file) {
        bool written = std::fprintf(file, "P6\n64 64\n255\n") > 0;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const unsigned long sample = XGetPixel(image, x, y);
            unsigned char rgb[3]{};
            for (int c = 0; c < 3; ++c) {
                unsigned long mask = masks[c], value = sample & mask;
                if (mask) {
                    while (!(mask & 1)) { mask >>= 1; value >>= 1; }
                    rgb[c] = (unsigned char)((value * 255) / mask);
                }
            }
            written = std::fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb) && written;
        }
        written = std::fclose(file) == 0 && written;
        check("front capture written", written);
    }
    XDestroyImage(image);
}

void secondaryTick(sapp_window window, void*) noexcept {
    check("one shared EGL context", eglGetCurrentContext() == sharedContext);
    check("secondary has its own surface", eglGetCurrentSurface(EGL_DRAW) != mainSurface);
    if (window.id == redWindow.id) {
        if (closeCaller) { sapp_destroy_window(greenWindow); closeCaller = false; }
        ++redDraws; clearColor(1, 0, 0);
    }
    else { ++greenDraws; clearColor(0, 1, 0); }
}

void event(const sapp_event* ev, sapp_window window, void*) noexcept {
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (window.id == redWindow.id && ev->key_code == SAPP_KEYCODE_A) ++redKeys;
        if (window.id == greenWindow.id && ev->key_code == SAPP_KEYCODE_B) ++greenKeys;
    }
    if (ev->type == SAPP_EVENTTYPE_RESIZED && window.id == redWindow.id &&
        ev->framebuffer_width == 83 && ev->framebuffer_height == 71) ++redResizes;
}

void closeRequested(sapp_window window, void*) noexcept {
    if (window.id == greenWindow.id) ++greenCloses;
    else check("close event targets green only", false);
}

void sendKey(Display* display, sapp_window window, KeySym symbol) noexcept {
    XEvent key{};
    key.xkey.type = KeyPress;
    key.xkey.display = display;
    key.xkey.window = (::Window)(uintptr_t)sapp_window_x11_get_window(window);
    key.xkey.root = DefaultRootWindow(display);
    key.xkey.keycode = XKeysymToKeycode(display, symbol);
    key.xkey.same_screen = True;
    XSendEvent(display, key.xkey.window, False, KeyPressMask, &key);
}

void init() noexcept {
    check("GLES3 X11 supports secondary windows", trussc::internal::supportsSecondaryWindows());
    sharedContext = eglGetCurrentContext();
    mainSurface = eglGetCurrentSurface(EGL_DRAW);
    check("main context and surface exist", sharedContext != EGL_NO_CONTEXT && mainSurface != EGL_NO_SURFACE);
    const auto* version = (const char*)glGetString(GL_VERSION);
    check("native context is OpenGL ES", version && std::strstr(version, "OpenGL ES"));
    sapp_window_desc desc{};
    desc.width = desc.height = 64;
    desc.no_high_dpi = true;
    desc.tick_cb = secondaryTick;
    desc.event_cb = event;
    desc.close_cb = closeRequested;
    desc.title = "EGL red";
    redWindow = sapp_create_window(&desc);
    desc.title = "EGL green";
    desc.no_high_dpi = false;
    greenWindow = sapp_create_window(&desc);
    check("two secondaries created", redWindow.id && greenWindow.id && redWindow.id != greenWindow.id);
    if (!redWindow.id || !greenWindow.id) { sapp_quit(); return; }
    auto* display = (Display*)sapp_x11_get_display();
    XMoveWindow(display, (::Window)(uintptr_t)sapp_x11_get_window(), 0, 0);
    XMoveWindow(display, (::Window)(uintptr_t)sapp_window_x11_get_window(redWindow), 160, 0);
    XMoveWindow(display, (::Window)(uintptr_t)sapp_window_x11_get_window(greenWindow), 320, 0);
    XSync(display, False);
    check("per-window DPI follows each creation request", sapp_window_dpi_scale(redWindow) == 1.0f &&
        sapp_window_dpi_scale(greenWindow) == sapp_dpi_scale());
    check("creation restores caller surface", eglGetCurrentSurface(EGL_DRAW) == mainSurface);
}

void frame() noexcept {
    if (!redWindow.id || !greenWindow.id) { sapp_quit(); return; }
    if (phase++ == 0) {
        clearColor(0, 0, 1);
        trussc::internal::tickX11WindowForTests(redWindow);
        check("red tick restores caller draw/read surfaces and context",
            eglGetCurrentSurface(EGL_DRAW) == mainSurface && eglGetCurrentSurface(EGL_READ) == mainSurface &&
            eglGetCurrentContext() == sharedContext);
        const int redBefore = redDraws;
        trussc::internal::tickX11WindowForTests(greenWindow);
        check("green presents independently", redDraws == redBefore && greenDraws > 0);
        captureFront(sapp_window_x11_get_window(redWindow), capturePaths[1].c_str(), 0);
        captureFront(sapp_window_x11_get_window(greenWindow), capturePaths[2].c_str(), 1);
        sapp_tc_gl_make_current(greenWindow);
        const EGLSurface greenSurface = eglGetCurrentSurface(EGL_DRAW);
        const EGLDisplay display = eglGetCurrentDisplay();
        check("distinct read/draw caller surfaces selected", eglMakeCurrent(display, mainSurface, greenSurface, sharedContext));
        trussc::internal::tickX11WindowForTests(redWindow);
        check("distinct read/draw caller surfaces restored", eglGetCurrentSurface(EGL_DRAW) == mainSurface &&
            eglGetCurrentSurface(EGL_READ) == greenSurface && eglGetCurrentContext() == sharedContext);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        trussc::internal::tickX11WindowForTests(redWindow);
        check("no-current caller is restored", eglGetCurrentContext() == EGL_NO_CONTEXT &&
            eglGetCurrentSurface(EGL_DRAW) == EGL_NO_SURFACE && eglGetCurrentSurface(EGL_READ) == EGL_NO_SURFACE);
        eglMakeCurrent(display, mainSurface, mainSurface, sharedContext);
        auto* xdisplay = (Display*)sapp_x11_get_display();
        XResizeWindow(xdisplay, (::Window)(uintptr_t)sapp_window_x11_get_window(redWindow), 83, 71);
        XSync(xdisplay, False);
        trussc::internal::tickX11WindowForTests(redWindow);
        sendKey(xdisplay, redWindow, XK_a);
        sendKey(xdisplay, greenWindow, XK_b);
        XEvent close{};
        close.xclient.type = ClientMessage;
        close.xclient.window = (::Window)(uintptr_t)sapp_window_x11_get_window(greenWindow);
        close.xclient.message_type = XInternAtom(xdisplay, "WM_PROTOCOLS", False);
        close.xclient.format = 32;
        close.xclient.data.l[0] = (long)XInternAtom(xdisplay, "WM_DELETE_WINDOW", False);
        XSendEvent(xdisplay, close.xclient.window, False, NoEventMask, &close);
        XSync(xdisplay, False);
    } else {
        check("X11 keys route independently", redKeys == 1 && greenKeys == 1);
        check("X11 resize routes to its window", redResizes == 1);
        check("X11 close routes to its window", greenCloses == 1);
        captureFront(sapp_x11_get_window(), capturePaths[0].c_str(), 2);
        sapp_tc_gl_make_current(greenWindow);
        closeCaller = true;
        trussc::internal::tickX11WindowForTests(redWindow);
        check("closing caller during another tick restores main", !sapp_window_valid(greenWindow) &&
            eglGetCurrentSurface(EGL_DRAW) == mainSurface && eglGetCurrentContext() == sharedContext);
        sapp_tc_gl_make_current(redWindow);
        sapp_destroy_window(redWindow);
        check("closing current secondary leaves main current", eglGetCurrentSurface(EGL_DRAW) == mainSurface &&
            eglGetCurrentContext() == sharedContext && !sapp_window_valid(redWindow));
        clearColor(0, 0, 1);
        unsigned char pixel[4]{};
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check("main still renders after secondary close", pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255);
        sapp_destroy_window(greenWindow);
        check("closing remaining secondary preserves main", eglGetCurrentSurface(EGL_DRAW) == mainSurface);
        check("no GL or EGL errors", glGetError() == GL_NO_ERROR && eglGetError() == EGL_SUCCESS);
        sapp_quit();
    }
}
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__) && defined(SOKOL_GLES3)
    if (argc > 1 && std::strcmp(argv[1], "--interactive") == 0) {
        tc::WindowSettings settings;
        settings.setSize(280, 220).setTitle("EGL Pi main");
        return tcCoreTest::runDisplayTest<InteractiveMain>(settings, failures, [] {});
    }
    if (argc > 1 && std::strcmp(argv[1], "--display") == 0) {
        failures = 0;
        const char* configuredPath = std::getenv("TC_EGL_CAPTURE_DIR");
        const auto captureDirectory = configuredPath ? tc::fs::path(configuredPath) :
            tc::fs::temp_directory_path() / "tc-egl-window-captures";
        tc::fs::create_directories(captureDirectory);
        capturePaths[0] = (captureDirectory / "egl-main.ppm").string();
        capturePaths[1] = (captureDirectory / "egl-red.ppm").string();
        capturePaths[2] = (captureDirectory / "egl-green.ppm").string();
        std::printf("EGL front captures: %s\n", captureDirectory.string().c_str());
        uint32_t previousId = 0;
        for (int cycle = 0; cycle < 2; ++cycle) {
            phase = redDraws = greenDraws = redKeys = greenKeys = greenCloses = redResizes = 0;
            closeCaller = false;
            sapp_desc desc{};
            desc.width = desc.height = 64;
            desc.window_title = "EGL main";
            desc.enable_clipboard = true;
            desc.enable_dragndrop = true;
            desc.allocator.alloc_fn = allocate;
            desc.allocator.free_fn = release;
            desc.init_cb = init;
            desc.frame_cb = frame;
            sapp_run(&desc);
            check("surface exercise completed", phase == 2);
            check("backend allocator releases its storage", liveAllocations == 0);
            check("repeated launch retains fresh native IDs", redWindow.id > previousId);
            previousId = greenWindow.id;
        }
        return failures ? 1 : 0;
    }
#else
    (void)argc; (void)argv;
#endif
#if defined(__linux__) && !defined(__ANDROID__) && defined(SOKOL_GLES3)
    bool warning = false;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        warning |= event.level == tc::LogLevel::Warning && event.message.find("require X11") != std::string::npos;
    });
    check("no-display creation fails explicitly", !tc::createWindow(tc::WindowSettings{}));
    check("no-display creation emits a Warning", warning);
    check("no-display attempt leaves main uninitialized", !sapp_isvalid());
    warning = false;
    const bool previouslyHeadless = tc::headless::active.exchange(true);
    const auto headlessWindow = tc::createWindow(tc::WindowSettings{});
    tc::headless::active.store(previouslyHeadless);
    check("headless no-display creation fails explicitly", !headlessWindow);
    check("headless no-display creation emits a Warning", warning);
    check("headless no-display attempt leaves main uninitialized", !sapp_isvalid());
    return failures ? 1 : 0;
#else
    std::puts("SKIP: eglWindowSurfaces needs GLES3 Linux X11 and --display");
    return 0;
#endif
}
