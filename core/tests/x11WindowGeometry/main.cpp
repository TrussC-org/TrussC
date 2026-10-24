// Compile the production helper against recording Xlib calls, without GPU/WM.
#ifdef TC_TEST_X11
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
namespace trussc {
struct IVec2 { int x, y; };
struct Log { template<class T> Log& operator<<(const T&) { return *this; } };
inline Log logWarning(const char*) { return {}; }
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
#include "../../platform/linux/tcWindowGeometryX11.h"
namespace {
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("x11WindowGeometry: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
}
int main() {
    _XPrivDisplay dpy = static_cast<_XPrivDisplay>(std::calloc(1, sizeof(*dpy)));
    Screen screen{}; screen.root = 1;
    dpy->screens = &screen; dpy->default_screen = 0;
    auto* display = reinterpret_cast<Display*>(dpy);
    using namespace trussc::internal;
    X11GeometryState main{640, 480}, secondary{321, 241};
    for (float scale : {1.f, 1.5f, 2.f}) {
        requests.clear();
        x11Geometry(display, 2, main, false, 0, 0, 641, 481, scale, true);
        check("resize only, truncating logical times DPI (#219)", requests.size() == 1 && requests[0].kind == "resize" && requests[0].width == long(641 * scale) && requests[0].height == long(481 * scale));
        check("last requested size is logical", main.requestedWidth == 641 && main.requestedHeight == 481);
    }
    requests.clear();
    x11Geometry(display, 2, main, true, -1920, 0, 0, 0, 2, true);
    const Atom full = atom(display, "_NET_WM_STATE_FULLSCREEN", False);
    const Atom vert = atom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    const Atom horz = atom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    check("NorthWestGravity pinned before state/move", normalHints.win_gravity == NorthWestGravity && (normalHints.flags & PWinGravity) && requests[0].kind == "gravity");
    check("leave fullscreen before move", requests[1].event.xclient.data.l[0] == 0 && requests[1].event.xclient.data.l[1] == long(full));
    check("one unconditional combined unmaximize (#219)", requests[2].event.xclient.data.l[0] == 0 && requests[2].event.xclient.data.l[1] == long(vert) && requests[2].event.xclient.data.l[2] == long(horz));
    check("move retains requested size, restores fullscreen last", requests.size() == 5 && requests[3].kind == "moveResize" && requests[3].x == -1920 && requests[3].width == 1282 && requests[3].height == 962 && requests[4].event.xclient.data.l[0] == 1 && requests[4].event.xclient.data.l[1] == long(full));
    requests.clear();
    x11Geometry(display, 3, secondary, true, -100, 20, 0, 0, 1.5f);
    check("secondary requested size is independent, fractional DPI truncates", requests.size() == 2 && requests[1].kind == "moveResize" && requests[1].window == 3 && requests[1].width == 481 && requests[1].height == 361);
    check("gravity already pinned does not overwrite other hints", normalHints.flags == PWinGravity);
    const auto actual = x11FramePosition(display, 2);
    check("query returns WM-clamped decorated frame", actual.x == 150 && actual.y == 40);
    malformedExtents = true;
    const auto invalid = x11FramePosition(display, 2);
    check("malformed frame extents cannot overflow", invalid.x == 155 && invalid.y == 66);
    const auto before = requests.size();
    x11Geometry(display, 2, main, true, INT_MAX, INT_MIN, 0, 0, 1);
    x11Geometry(display, 2, main, false, 0, 0, INT_MAX, 1, 2);
    x11Geometry(display, 2, main, false, 0, 0, -1, 10, 1);
    x11Geometry(display, 2, main, true, 0, 0, 0, 0, 1000);
    check("invalid protocol requests send nothing or change intent", requests.size() == before && main.requestedWidth == 641);
    X11GeometryState unknown;
    requests.clear();
    x11Geometry(display, 4, unknown, true, -500, 30, 0, 0, 1);
    check("unknown requested size uses move alone (#219)", requests.size() == 2 && requests.back().kind == "move" && requests.back().x == -500);
    // #219 sends based on intent without waiting for _NET_WM_STATE; no property
    // round trip or _NET_SUPPORTED/_NET_MOVERESIZE_WINDOW policy is involved.
    requests.clear();
    x11Geometry(display, 3, secondary, true, 10, 20, 0, 0, 1, true);
    check("creation-time fullscreen intent is preserved", requests.front().event.xclient.data.l[1] == long(full) && requests.back().event.xclient.data.l[0] == 1);
    std::free(dpy);
    return failures ? 1 : 0;
}
#else
#include <cstdio>
int main() { std::puts("SKIP: Linux X11 geometry recording seam"); }
#endif
