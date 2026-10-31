// Template for test_window_pixel_size.py: inject production control and global
// size bodies, and compile the production helper against recording Xlib calls.

#include "core/include/sokol/util/sokol_window_size.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <climits>
#include "core/include/tc/app/internal/tcWindowFullscreen.h"
namespace trussc {
struct IVec2 { int x, y; };
struct Log { template<class T> Log& operator<<(const T&) { return *this; } };
int warnings = 0;
inline Log logWarning(const char*) { ++warnings; return {}; }
}
namespace {
std::map<std::string, Atom> atoms;
struct Request {
    std::string kind;
    ::Window window;
    long x = 0, y = 0, width = 0, height = 0;
    XEvent event{};
};
std::vector<Request> requests;
XSizeHints normalHints{};
bool malformedExtents = false;
Atom atom(Display*, const char* name, Bool) {
    auto found = atoms.find(name);
    if (found != atoms.end()) return found->second;
    return atoms.emplace(name, atoms.size() + 1).first->second;
}
int readProperty(Display*, ::Window, Atom, long, long, Bool, Atom,
                 Atom* type, int* format, unsigned long* count, unsigned long* remaining, unsigned char** data) {
    *type = XA_CARDINAL; *format = 32; *count = 4; *remaining = 0;
    unsigned long extents[] = {5, 5, 26, 5};
    if (malformedExtents) extents[0] = ULONG_MAX;
    *data = static_cast<unsigned char*>(std::malloc(sizeof(extents)));
    std::memcpy(*data, extents, sizeof(extents));
    return Success;
}
int normal(Display*, ::Window, XSizeHints* hints, long* supplied) { *hints = normalHints; *supplied = normalHints.flags; return 1; }
int setNormal(Display*, ::Window win, XSizeHints* hints) { normalHints = *hints; requests.push_back({"gravity", win}); return 1; }
int translate(Display*, ::Window, ::Window, int, int, int* x, int* y, ::Window* child) { *x = 155; *y = 66; *child = 0; return 1; }
int send(Display*, ::Window, Bool, long, XEvent* event) { requests.push_back({"state", event->xclient.window, 0, 0, 0, 0, *event}); return 1; }
int recordResize(Display*, ::Window win, unsigned w, unsigned h) { requests.push_back({"resize", win, 0, 0, long(w), long(h)}); return 1; }
int moveResize(Display*, ::Window win, int x, int y, unsigned w, unsigned h) { requests.push_back({"moveResize", win, x, y, long(w), long(h)}); return 1; }
int moveWindow(Display*, ::Window win, int x, int y) { requests.push_back({"move", win, x, y}); return 1; }
int flush(Display*) { return 1; }
int release(void* data) { std::free(data); return 1; }
}
#define XInternAtom atom
#define XGetWindowProperty readProperty
#define XGetWMNormalHints normal
#define XSetWMNormalHints setNormal
#define XTranslateCoordinates translate
#define XSendEvent send
#define XResizeWindow recordResize
#define XMoveResizeWindow moveResize
#define XMoveWindow moveWindow
#define XFlush flush
#define XFree release
#include "core/platform/linux/tcWindowGeometryX11.h"

#include <limits>
#include <cassert>
namespace trussc {
struct Window;
struct RecordingOperations {
    void (*setTitle)(Window&, const char*) = nullptr;
    void (*setSize)(Window&, double, double, bool) = nullptr;
    void (*setPosition)(Window&, int, int) = nullptr;
    IVec2 (*position)(const Window&) = nullptr;
    void (*setFullscreen)(Window&, bool) = nullptr;
    void (*toggleFullscreen)(Window&) = nullptr;
};
extern const RecordingOperations recordingOperations;

float dpi=1;
float sapp_dpi_scale() { return dpi; }
bool sapp_isvalid() { return true; }
bool sapp_is_fullscreen() { return false; }
void sapp_toggle_fullscreen() {}
void sapp_set_window_title(const char*) {}
namespace internal {
enum class WindowControl { Title, Size, PixelSize, Position, Fullscreen, PositionQuery, ToggleFullscreen };
constexpr bool windowSizeIsScreenControlled() { return false; }
constexpr bool supportsSecondaryWindows() { return TEST_SECONDARY; }
using Hook = bool (*)(Window&, WindowControl, int, int, const std::string*, IVec2&);
Hook windowControlHookForTests() { return nullptr; }
Display* dpy=nullptr;
X11GeometryState geometry{640,480};
struct WindowRequestAccess {
    static bool ignoreGeometry(Window&, WindowGeometryCall) { return false; }
};
void setSingleWindowFullscreen(bool) {}
WindowFullscreenPhase singleWindowFullscreenPhase() { return WindowFullscreenPhase::Windowed; }
bool ignoreFullscreenGeometry(WindowFullscreenPhase, WindowGeometryCall) { return false; }
bool routeSetWindowSizeToWindow(int, int, WindowGeometryCall) { return false; }
struct StartupDesc { int width, height, _tc_pixel_width, _tc_pixel_height; };
void seedCreationGeometry(const StartupDesc& desc) { CREATION_GEOMETRY_SEED }
void setSingleWindowSizeLogical(int w, int h) { x11Geometry(dpy, 2, geometry, false, 0,0,w,h,dpi); }
void setSingleWindowSizeFractionalX11(double w, double h) { x11Geometry(dpy, 2, geometry, false, 0,0,w,h,dpi,false,true); }
void setSingleWindowPosition(int, int) {}
IVec2 singleWindowPosition() { return {}; }
bool pixelPerfectMode() { return true; }
bool routeSetWindowPixelSizeToWindow(int,int);
}
namespace mcp::internal { std::string markedWindowTitle(const std::string& title) { return title; } }
struct Window {
    struct { bool isMain=true; float dpiScale=1; } ctx_;
    std::string title_;
    struct { const RecordingOperations* operations = &recordingOperations; } binding_;
    bool isOpen() const { return true; }
    IVec2 control(internal::WindowControl, int, int, const std::string* =nullptr, internal::WindowGeometryCall = internal::WindowGeometryCall::WindowSize);
};
const RecordingOperations recordingOperations{nullptr,
    [](Window&, double width, double height, bool pixels) {
        internal::x11Geometry(internal::dpy, 2, internal::geometry, false, 0, 0,
                              width, height, dpi, false, pixels);
    }};
CONTROL_BODY
Window mainWindow;
bool routeToMain=true;
namespace internal {
bool routeSetWindowPixelSizeToWindow(int w,int h) {
    if (!routeToMain) return false;
    mainWindow.control(WindowControl::PixelSize,w,h);
    return true;
}
}
void setWindowSizeLogical(int w,int h) { internal::setSingleWindowSizeLogical(w,h); }
GLOBAL_BODY
}
int main() {
    _XPrivDisplay dpy=static_cast<_XPrivDisplay>(std::calloc(1,sizeof(*dpy)));
    Screen screen{}; screen.root=1; dpy->screens=&screen;
    trussc::internal::dpy=reinterpret_cast<Display*>(dpy);
    for (float scale : {1.25f,1.5f,1.75f,2.f}) {
        trussc::dpi=scale; trussc::mainWindow.ctx_.dpiScale=scale;
        for (int path : {0,1,2}) {
            trussc::routeToMain=path == 1; requests.clear();
            if (path == 0) trussc::mainWindow.control(trussc::internal::WindowControl::PixelSize,1001,667);
            else trussc::setWindowSize(1001,667);
            if (path == 2) { assert(requests.empty()); continue; }
            assert(!requests.empty());
            const auto& r=requests.back();
            assert(r.kind == "resize" && r.width == 1001 && r.height == 667);
            assert(trussc::internal::geometry.pixelSize);
        }
        // The first move must retain the converted creation size. In logical
        // mode it keeps the existing truncation on moves, including odd sizes.
        for (bool pixelStartup : {false, true}) for (int extent : {480, 640, 1001}) {
            trussc::internal::seedCreationGeometry(
                {extent, 667, pixelStartup ? extent : 0, pixelStartup ? 667 : 0});
            requests.clear();
            trussc::internal::x11Geometry(trussc::internal::dpy, 2,
                trussc::internal::geometry, true, 20, 30, 0, 0, scale);
            const auto& r = requests.back();
            const int expectedWidth = pixelStartup ? extent : int(float(extent) * scale);
            const int expectedHeight = pixelStartup ? 667 : int(667.0f * scale);
            assert(r.kind == "moveResize" && r.width == expectedWidth && r.height == expectedHeight);
            assert(trussc::internal::geometry.pixelSize == pixelStartup);
        }
    }
    // A main window without X11 (KMS) keeps the existing Warning/no-request behavior.
    trussc::internal::dpy = nullptr;
    for (bool routed : {false, true}) {
        trussc::routeToMain = routed;
        requests.clear();
        const int before = trussc::warnings;
        trussc::setWindowSize(1001,667);
        assert(requests.empty() && trussc::warnings == before + (routed ? 1 : 0));
    }
    std::free(dpy);
}
