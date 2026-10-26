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
#include <time.h>
// Private native test entry, compiled only into the combined test backend.
namespace trussc::internal { void tickX11WindowForTests(sapp_window window); }
static unsigned makeCurrentCalls;
extern "C" EGLBoolean __real_eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
extern "C" EGLBoolean __wrap_eglMakeCurrent(EGLDisplay display, EGLSurface draw, EGLSurface read, EGLContext context) {
    ++makeCurrentCalls;
    return __real_eglMakeCurrent(display, draw, read, context);
}
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
int greenResizes = 0, lastRedWidth = 0, lastRedHeight = 0;
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

double monotonicTime() noexcept {
    timespec value{};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (double)value.tv_sec + (double)value.tv_nsec * 1e-9;
}

template<class Ready>
bool waitForGeometry(Ready ready) noexcept {
    // Bound (asynchronous WM geometry): XSync cannot wait for the WM; cap the wait at two seconds, never assert latency.
    const double deadline = monotonicTime() + 2.0;
    do {
        if (ready()) return true;
        const timespec pause{0, 5000000};
        nanosleep(&pause, nullptr);
    } while (monotonicTime() < deadline);
    return false;
}

bool captureWindowsReady() noexcept {
    auto* display = (Display*)sapp_x11_get_display();
    const ::Window windows[] = {
        (::Window)(uintptr_t)sapp_x11_get_window(),
        (::Window)(uintptr_t)sapp_window_x11_get_window(redWindow),
        (::Window)(uintptr_t)sapp_window_x11_get_window(greenWindow)
    };
    int x[3]{}, y[3]{};
    XWindowAttributes attributes[3]{};
    for (int i = 0; i < 3; ++i) {
        ::Window child{};
        if (!XGetWindowAttributes(display, windows[i], &attributes[i]) ||
            attributes[i].map_state != IsViewable || attributes[i].width < 64 || attributes[i].height < 64 ||
            !XTranslateCoordinates(display, windows[i], DefaultRootWindow(display), 0, 0, &x[i], &y[i], &child))
            return false;
        for (int j = 0; j < i; ++j) {
            if (x[i] < x[j] + attributes[j].width && x[j] < x[i] + attributes[i].width &&
                y[i] < y[j] + attributes[j].height && y[j] < y[i] + attributes[i].height)
                return false;
        }
    }
    return true;
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
    if (ev->type == SAPP_EVENTTYPE_RESIZED) {
        const bool finalSize = ev->framebuffer_width == 83 && ev->framebuffer_height == 71;
        if (window.id == redWindow.id) {
            lastRedWidth = ev->framebuffer_width;
            lastRedHeight = ev->framebuffer_height;
            if (finalSize) ++redResizes;
        } else if (window.id == greenWindow.id && finalSize) ++greenResizes;
    }
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
        // A real WM can apply the initial moves asynchronously, too. Captures
        // need viewable, separated windows, not exact WM decoration offsets.
        const bool placed = waitForGeometry(captureWindowsReady);
        check("initial capture geometry applied (timeout if not viewable and separated)", placed);
        if (!placed) { sapp_quit(); return; }
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
        const bool resized = waitForGeometry([] {
            trussc::internal::tickX11WindowForTests(redWindow);
            return redResizes > 0;
        });
        if (!resized) std::printf("resize timeout: wanted 83x71 on red, last resize=%dx%d, red=%d green=%d\n",
            lastRedWidth, lastRedHeight, redResizes, greenResizes);
        check("WM applied red 83x71 resize before timeout", resized);
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
        // Intermediate WM sizes are valid. The requested final size must be
        // delivered exactly once to red, never green, and remain the last size.
        trussc::internal::tickX11WindowForTests(redWindow);
        trussc::internal::tickX11WindowForTests(greenWindow);
        check("X11 resize routes to its window", redResizes == 1 && greenResizes == 0 &&
            lastRedWidth == 83 && lastRedHeight == 71 &&
            sapp_window_framebuffer_width(redWindow) == 83 && sapp_window_framebuffer_height(redWindow) == 71);
        check("X11 close routes to its window", greenCloses == 1);
        sapp_window_desc spareDesc{};
        spareDesc.width = spareDesc.height = 32;
        spareDesc.title = "non-current destroy";
        const sapp_window spare = sapp_create_window(&spareDesc);
        check("non-current destroy fixture created", sapp_window_valid(spare));
        const EGLDisplay display = eglGetCurrentDisplay();
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        const unsigned beforeDestroy = makeCurrentCalls;
        sapp_destroy_window(spare);
        check("destroying non-current secondary preserves unbound caller without make-current",
            makeCurrentCalls == beforeDestroy && eglGetCurrentContext() == EGL_NO_CONTEXT);
        eglMakeCurrent(display, mainSurface, mainSurface, sharedContext);
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

// Single-window device exercise, also suitable for the Pi 4 regression pass.
bool mainIdle, mainDirty = true;
double inputTime;
void mainProbeInit() noexcept {
    const char* version = (const char*)glGetString(GL_VERSION);
    check("main probe uses GLES", version && std::strstr(version, "OpenGL ES"));
    EGLint id = 0, count = 0;
    EGLConfig config{};
    const EGLDisplay display = eglGetCurrentDisplay();
    eglQuerySurface(display, eglGetCurrentSurface(EGL_DRAW), EGL_CONFIG_ID, &id);
    const EGLint attrs[] = { EGL_CONFIG_ID, id, EGL_NONE };
    check("main probe config available", eglChooseConfig(display, attrs, &config, 1, &count) && count == 1);
    for (EGLint attr : { EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE,
                        EGL_DEPTH_SIZE, EGL_STENCIL_SIZE, EGL_SAMPLES }) {
        EGLint value = 0;
        eglGetConfigAttrib(display, config, attr, &value);
        std::printf("main config attribute 0x%x = %d\n", attr, value);
    }
    std::printf("main GLES=%s dpi=%g size=%dx%d samples=%d interval=%d\n", version ? version : "?",
        sapp_dpi_scale(), sapp_width(), sapp_height(), sapp_sample_count(), sapp_query_desc().swap_interval);
    std::puts("main probe: E idle | R redraw | L lock | H cursor hide | 1/2/3 cursor shapes\n"
        "C copy oversize | O copy old | P paste | T title | F fullscreen | Q quit\n"
        "Drag files here; type text; click/wheel while locked; verify anchor and raw deltas in log.");
}
void mainProbeFrame() noexcept {
    if (mainIdle && !mainDirty) { sapp_skip_present(); return; }
    mainDirty = false;
    clearColor(0, .25f, .5f);
    if (inputTime != 0) {
        std::printf("main input-to-next-frame %.3f ms\n", (monotonicTime() - inputTime) * 1000);
        inputTime = 0;
    }
}
void mainProbeEvent(const sapp_event* e) noexcept {
    inputTime = monotonicTime();
    mainDirty = true;
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN) {
        switch (e->key_code) {
            case SAPP_KEYCODE_E: mainIdle = !mainIdle; break;
            case SAPP_KEYCODE_R: break;
            case SAPP_KEYCODE_L: sapp_lock_mouse(!sapp_mouse_locked()); break;
            case SAPP_KEYCODE_H: sapp_show_mouse(!sapp_mouse_shown()); break;
            case SAPP_KEYCODE_1: sapp_set_mouse_cursor(SAPP_MOUSECURSOR_ARROW); break;
            case SAPP_KEYCODE_2: sapp_set_mouse_cursor(SAPP_MOUSECURSOR_IBEAM); break;
            case SAPP_KEYCODE_3: sapp_set_mouse_cursor(SAPP_MOUSECURSOR_CROSSHAIR); break;
            case SAPP_KEYCODE_C: sapp_set_clipboard_string("123456789"); break;
            case SAPP_KEYCODE_O: sapp_set_clipboard_string("old"); break;
            case SAPP_KEYCODE_P: {
                const char* text = sapp_get_clipboard_string();
                std::printf("main clipboard: %s\n", text ? text : "<failed transfer>");
                break;
            }
            case SAPP_KEYCODE_T: sapp_set_window_title("EGL main title updated"); break;
            case SAPP_KEYCODE_F: sapp_toggle_fullscreen(); break;
            case SAPP_KEYCODE_Q: sapp_request_quit(); break;
            default: break;
        }
    }
    std::printf("main event=%d pos=(%g,%g) delta=(%g,%g) locked=%d char=U+%04x size=%dx%d\n",
        e->type, e->mouse_x, e->mouse_y, e->mouse_dx, e->mouse_dy, sapp_mouse_locked(),
        e->char_code, e->window_width, e->window_height);
    if (e->type == SAPP_EVENTTYPE_FILES_DROPPED) {
        for (int i = 0; i < sapp_get_num_dropped_files(); ++i)
            std::printf("main dropped: %s\n", sapp_get_dropped_file_path(i));
    }
}
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__) && defined(SOKOL_GLES3)
    if (argc > 1 && std::strcmp(argv[1], "--main-regression") == 0) {
        sapp_desc desc{};
        desc.width = 333; desc.height = 257; desc.high_dpi = true;
        desc.window_title = "EGL main regression"; desc.swap_interval = 4;
        desc.enable_clipboard = desc.enable_dragndrop = true;
        desc.clipboard_size = 8;
        desc.init_cb = mainProbeInit; desc.frame_cb = mainProbeFrame; desc.event_cb = mainProbeEvent;
        desc.allocator.alloc_fn = allocate; desc.allocator.free_fn = release;
        const uint32_t pixels[] = { 0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff };
        desc.icon.images[0].width = desc.icon.images[0].height = 2;
        desc.icon.images[0].pixels.ptr = pixels; desc.icon.images[0].pixels.size = sizeof(pixels);
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--msaa4") == 0) desc.sample_count = 4;
            if (std::strcmp(argv[i], "--fullscreen") == 0) desc.fullscreen = true;
            if (std::strcmp(argv[i], "--no-high-dpi") == 0) desc.high_dpi = false;
        }
        sapp_run(&desc);
        check("main probe allocator balanced", liveAllocations == 0);
        return failures ? 1 : 0;
    }
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
            greenResizes = lastRedWidth = lastRedHeight = 0;
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
